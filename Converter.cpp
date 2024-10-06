#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <omp.h>
#include <utility> 
#include <algorithm>
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
	#include <libavutil/samplefmt.h>
}




void process_decoded_frame(AVFrame *frame);

std::vector<std::pair<int, int>> create_chunks(const std::string &input_file, int chunk_duration) {
    AVFormatContext *fmt_ctx = nullptr;
    avformat_open_input(&fmt_ctx, input_file.c_str(), nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);

    // Get the total duration in seconds
    int64_t total_duration = fmt_ctx->duration / AV_TIME_BASE; // in seconds
    int num_chunks = (total_duration + chunk_duration - 1) / chunk_duration; // Calculate number of chunks
    std::vector<std::pair<int, int>> chunks(num_chunks);

    #pragma omp parallel for
    for (int i = 0; i < num_chunks; ++i) {
        int start_time = i * chunk_duration;
        int end_time = std::min(start_time + chunk_duration, (int)total_duration);
        chunks[i] = {start_time, end_time};
    }

    avformat_close_input(&fmt_ctx);
    return chunks;
}


std::vector<std::pair<AVFrame*, int64_t>> decode_audio_chunk(AVCodecContext *dec_ctx, int stream_index, AVFormatContext *fmt_ctx, AVStream *audio_stream, int start_time, int end_time) {
    std::vector<std::pair<AVFrame*, int64_t>> decoded_frames;
    AVPacket *packet = av_packet_alloc();

    // Seek to the start time
    int64_t seek_target = av_rescale_q(start_time * AV_TIME_BASE, AV_TIME_BASE_Q, audio_stream->time_base);
    if (av_seek_frame(fmt_ctx, stream_index, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
        std::cerr << "Error seeking to start time" << std::endl;
        av_packet_free(&packet);
        return decoded_frames;
    }

    // Flush the decoder
    avcodec_flush_buffers(dec_ctx);

    while (av_read_frame(fmt_ctx, packet) >= 0) {
        if (packet->stream_index != stream_index) {
            av_packet_unref(packet);
            continue;
        }

        int64_t pts_time = av_rescale_q(packet->pts, audio_stream->time_base, AV_TIME_BASE_Q) / AV_TIME_BASE;
        if (pts_time > end_time) {
            av_packet_unref(packet);
            break;
        }

        int ret = avcodec_send_packet(dec_ctx, packet);
        if (ret < 0) {
            std::cerr << "Error sending packet for decoding" << std::endl;
            av_packet_unref(packet);
            continue;
        }

        while (ret >= 0) {
            AVFrame *frame = av_frame_alloc();
            ret = avcodec_receive_frame(dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                av_frame_free(&frame);
                break;
            } else if (ret < 0) {
                std::cerr << "Error during decoding" << std::endl;
                av_frame_free(&frame);
                break;
            }

            int64_t frame_pts = av_rescale_q(frame->pts, audio_stream->time_base, AV_TIME_BASE_Q) / AV_TIME_BASE;
            if (frame_pts >= start_time && frame_pts <= end_time) {
                decoded_frames.emplace_back(frame, frame_pts);
            } else {
                av_frame_free(&frame);
            }
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return decoded_frames;
}





std::vector<std::pair<AVFrame*, int64_t>> decode_data(const std::string &input_file, int chunk_duration) {
    // Initialize FFmpeg
    //av_register_all();

    // Open the input file and find stream info
    AVFormatContext *fmt_ctx = nullptr;
    avformat_open_input(&fmt_ctx, input_file.c_str(), nullptr, nullptr);
    avformat_find_stream_info(fmt_ctx, nullptr);

    // Get the best audio stream
    int stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVStream *audio_stream = fmt_ctx->streams[stream_index];
    enum AVCodecID codec_id = audio_stream->codecpar->codec_id;

    // Initialize the decoder
    const AVCodec *decoder = avcodec_find_decoder(codec_id);
    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(dec_ctx, audio_stream->codecpar);
    avcodec_open2(dec_ctx, decoder, nullptr);

    // Create chunks
    auto chunks = create_chunks(input_file, chunk_duration);
    
    std::vector<std::pair<AVFrame*, int64_t>> all_decoded_frames;

    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < chunks.size(); ++i) {
        int start_time = chunks[i].first;
        int end_time = chunks[i].second;
        auto decoded_chunk = decode_audio_chunk(dec_ctx,stream_index,fmt_ctx, audio_stream, start_time, end_time);
        
        #pragma omp critical
        {
            all_decoded_frames.insert(all_decoded_frames.end(), decoded_chunk.begin(), decoded_chunk.end());
        }
    }

    // Sort frames by their timestamps
    std::sort(all_decoded_frames.begin(), all_decoded_frames.end(), [](const auto& a,const auto& b) {
        return a.second < b.second; // Sort by timestamp
    });


    // Clean up
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    return all_decoded_frames;
}

void adjust_volume(AVFrame* frame, float volume_multiplier) {
    int data_size = av_get_bytes_per_sample((AVSampleFormat)frame->format);
    if (data_size < 0) {
        return; // Invalid data size
    }

    for (int channel = 0; channel < frame->ch_layout.nb_channels; channel++) {
        uint8_t* channel_data = frame->data[channel];

        for (int sample = 0; sample < frame->nb_samples; sample++) {
            if (frame->format == AV_SAMPLE_FMT_FLT || frame->format == AV_SAMPLE_FMT_FLTP) {
                float* sample_data = (float*)(channel_data + sample * data_size);
                *sample_data *= volume_multiplier;
            } else if (frame->format == AV_SAMPLE_FMT_S16 || frame->format == AV_SAMPLE_FMT_S16P) {
                int16_t* sample_data = (int16_t*)(channel_data + sample * data_size);
                *sample_data = std::clamp(static_cast<int32_t>(*sample_data * volume_multiplier), INT16_MIN, INT16_MAX);
            } else if (frame->format == AV_SAMPLE_FMT_S32 || frame->format == AV_SAMPLE_FMT_S32P) {
                int32_t* sample_data = (int32_t*)(channel_data + sample * data_size);
                *sample_data = std::clamp(static_cast<int32_t>(*sample_data * volume_multiplier), INT32_MIN, INT32_MAX);
            }
            // Handle other formats similarly (if needed)
        }
    }
}



void process_decoded_frame(AVFrame* frame){
	adjust_volume(frame,1.1);
}



void encode_audio_chunks(const std::vector<std::pair<AVFrame*, int64_t>>& frames, const std::string& output_file) {
    // Initialize FFmpeg encoder
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (!encoder) {
        // Handle error: encoder not found
        return;
    }

    AVCodecContext *enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        // Handle error: context allocation failed
        return;
    }

    // Set encoder parameters
    
	AVChannelLayout ch_layout;
	// Set channel layout to stereo
	av_channel_layout_default(&ch_layout, 2);    
    
    enc_ctx->sample_rate = frames[0].first->sample_rate;  // Use sample rate from the first frame
    enc_ctx->ch_layout =  ch_layout;  // Use channel layout from the first frame
    //enc_ctx->ch_layout = enc_ctx->ch_layout.nb_channels;
    enc_ctx->bit_rate = 192000; // Set bit rate (adjust as needed)
    enc_ctx->time_base = {1, enc_ctx->sample_rate}; // Time base for audio

    // Set the codec's sample format
    enc_ctx->sample_fmt = encoder->sample_fmts[0]; // Use the first supported sample format

    // Open the encoder
    if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
        // Handle error: could not open encoder
        avcodec_free_context(&enc_ctx);
        return;
    }

    std::vector<std::pair<AVPacket*, int64_t>> encoded_chunks(frames.size());

    #pragma omp parallel for
    for (size_t i = 0; i < frames.size(); i++) {
        AVFrame* frame = frames[i].first;
        AVPacket* pkt = av_packet_alloc();
        
        // Encode the frame
        int ret = avcodec_send_frame(enc_ctx, frame);
        if (ret < 0) {
            // Handle error
            av_packet_free(&pkt);
            continue;
        }

        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret < 0) {
            // Handle error
            av_packet_free(&pkt);
            continue;
        }

        encoded_chunks[i] = { pkt, frames[i].second }; // Store encoded packet with timestamp
    }

    // Write to output file in order
    std::ofstream output(output_file, std::ios::binary);
    for (const auto& encoded_chunk : encoded_chunks) {
        AVPacket* pkt = encoded_chunk.first;
        output.write(reinterpret_cast<const char*>(pkt->data), pkt->size);
        av_packet_free(&pkt); // Free the packet after writing
    }

    // Clean up
    avcodec_free_context(&enc_ctx);
    output.close();
}

int main() {
    std::string input_file = "Sample_mp3_1.mp3";
    std::string output_file = "sample_out.mp3";
    int chunk_duration = 10; // Chunk size in seconds

    // Initialize FFmpeg
    // av_register_all(); // This is not needed for newer FFmpeg versions

    std::vector<std::pair<AVFrame*, int64_t>> decoded_frames = decode_data(input_file, chunk_duration);

    if (decoded_frames.empty()) {
        std::cerr << "No frames were decoded. Exiting." << std::endl;
        return 1;
    }

    int sample_rate = decoded_frames[0].first->sample_rate;
    for (const auto& frame_pair : decoded_frames) {
        if (frame_pair.first->sample_rate != sample_rate) {
            std::cerr << "Inconsistent sample rates detected!" << std::endl;
            // Clean up decoded frames before exiting
            for (auto& fp : decoded_frames) {
                av_frame_free(&(fp.first));
            }
            return 1;
        }
    }

    for (auto& frame_pair : decoded_frames) {
        process_decoded_frame(frame_pair.first);
    }

    encode_audio_chunks(decoded_frames, output_file);

    // Clean up
    for (auto& frame_pair : decoded_frames) {
        av_frame_free(&(frame_pair.first));
    }

    return 0;
}

