#include <iostream>
#include <vector>
#include <utility>
#include <string>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>

using namespace std;

vector<pair<AVFrame*, int64_t>> decode(const string& input_filename) {
    vector<pair<AVFrame*, int64_t>> frames;
    AVFormatContext* format_ctx = nullptr;
    const AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // Open input file
    if (avformat_open_input(&format_ctx, input_filename.c_str(), nullptr, nullptr) < 0) {
        cerr << "Error opening input file." << endl;
        return frames;
    }

    // Find the audio stream
    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        cerr << "Error finding stream info." << endl;
        return frames;
    }

    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        codec = avcodec_find_decoder(format_ctx->streams[i]->codecpar->codec_id);
        if (codec && format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            codec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(codec_ctx, format_ctx->streams[i]->codecpar);
            avcodec_open2(codec_ctx, codec, nullptr);
            break;
        }
    }

    // Read frames from the input file
    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == i) {
            avcodec_send_packet(codec_ctx, packet);
            while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                frames.emplace_back(make_pair(av_frame_clone(frame), frame->pts));
            }
        }
        av_packet_unref(packet);
    }

    process(frames, codec_ctx);
    // Clean up
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);

    return frames;
}

void process(vector<pair<AVFrame*, int64_t>>& frames, const AVCodecContext* codec_ctx) {
    for (auto& frame_pair : frames) {
        AVFrame* frame = frame_pair.first;
        for (int i = 0; i < frame->nb_samples; i++) {
            for (int ch = 0; ch < codec_ctx->ch_layout.nb_channels; ch++) {
                ((float*)frame->data[ch])[i] *= 0.5; // Decrease volume by half
            }
        }
    }
}

void encode(const string& output_filename, const vector<pair<AVFrame*, int64_t>>& frames, AVCodecID codec_id) {
    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = avcodec_find_encoder(codec_id);
    
    if (!codec) {
        cerr << "Codec not found." << endl;
        return;
    }

    avformat_alloc_output_context2(&format_ctx, nullptr, nullptr, output_filename.c_str());
    
    AVStream* stream = avformat_new_stream(format_ctx, codec);
    codec_ctx = avcodec_alloc_context3(codec);

    codec_ctx->bit_rate = 64000; // Set bitrate
    codec_ctx->sample_rate = 44100; // Set sample rate
    codec_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO; // Set channel layout
    codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;

    // Set codec parameters
    avcodec_parameters_from_context(stream->codecpar, codec_ctx);

    // Open the output file
    if (avio_open(&format_ctx->pb, output_filename.c_str(), AVIO_FLAG_WRITE) < 0) {
        cerr << "Could not open output file." << endl;
        return;
    }

    // Write header and check for errors
    int ret = avformat_write_header(format_ctx, nullptr);
    if (ret < 0) {
        cerr << "Error writing header: " << av_err2str(ret) << endl;
        return;
    }

    for (const auto& frame_pair : frames) {
        AVFrame* frame = frame_pair.first;

        // Send the frame to the encoder
        avcodec_send_frame(codec_ctx, frame);
        AVPacket* packet = av_packet_alloc();
        
        while (avcodec_receive_packet(codec_ctx, packet) >= 0) {
            av_interleaved_write_frame(format_ctx, packet);
            av_packet_unref(packet);
        }
        av_frame_free(&frame);
    }

    av_write_trailer(format_ctx);
    avio_close(format_ctx->pb);
    avcodec_free_context(&codec_ctx);
    avformat_free_context(format_ctx);
}

int main() {
    const string input_file = "input.mp3"; // Replace with your input file
    const string output_file = "output.mp3"; // Replace with your output file

    // Decode
    auto frames = decode(input_file);

    // Process
    //process(frames);

    // Encode
    encode(output_file, frames, AV_CODEC_ID_MP3); // Use appropriate codec ID

    return 0;
}

