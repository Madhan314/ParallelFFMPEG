#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <omp.h>
#include <algorithm>
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
    #include <libavutil/opt.h>
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

/*std::vector<AVFrame*> decode_audio_chunk(AVCodecContext *dec_ctx, int start_time, int end_time) {
    std::vector<AVFrame*> frames; // Vector to store decoded frames

    // Seek to the start time
    av_seek_frame(fmt_ctx, stream_index, start_time * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);
    
    AVPacket packet;
    while (av_read_frame(fmt_ctx, &packet) >= 0) {
        // Check if we are past the end time
        if (packet.pts * av_q2d(audio_stream->time_base) > end_time) {
            av_packet_unref(&packet);
            break; // Stop decoding if we've passed the end time
        }

        // Send the packet to the decoder
        avcodec_send_packet(dec_ctx, &packet);

        // Receive decoded frames
        AVFrame *frame = av_frame_alloc(); // Allocate a new frame
        while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
            frames.push_back(frame); // Store the frame in the vector
        }

        av_packet_unref(&packet);
    }

    return frames; // Return the vector of frames
}*/

std::vector<std::pair<AVFrame*, int64_t>> decode_audio_chunk(AVCodecContext *dec_ctx,int stream_index, AVFormatContext *fmt_ctx, AVStream *audio_stream, int start_time, int end_time) {
    std::vector<std::pair<AVFrame*, int64_t>> decoded_frames;
    AVPacket packet;

    av_seek_frame(fmt_ctx, stream_index, start_time * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);

    while (av_read_frame(fmt_ctx, &packet) >= 0) {
        if (packet.pts * av_q2d(audio_stream->time_base) > end_time) {
            av_packet_unref(&packet);
            break;
        }

        avcodec_send_packet(dec_ctx, &packet);

        AVFrame *frame = av_frame_alloc();
        while (avcodec_receive_frame(dec_ctx, frame) >= 0) {
            int64_t timestamp = frame->pts * av_q2d(audio_stream->time_base);
            decoded_frames.emplace_back(frame, timestamp);
        }

        av_packet_unref(&packet);
    }

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
    
    /*std::vector<AVFrame*> all_decoded_frames; // Vector to store all decoded frames
    
    #pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < chunks.size(); ++i) {
        int start_time = chunks[i].first;
        int end_time = chunks[i].second;
        auto frames = decode_audio_chunk(dec_ctx, start_time, end_time); // Collect frames
        
        // Merge frames into all_decoded_frames (this part is critical)
        #pragma omp critical
        {
            all_decoded_frames.insert(all_decoded_frames.end(), frames.begin(), frames.end());
        }
    }

    // Now you can process the decoded frames in the main function
    for (AVFrame* frame : all_decoded_frames) {
        process_decoded_frame(frame); // Process each decoded frame
        av_frame_free(&frame); // Free frame after processing
    }*/
    
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


int main() {
    std::string input_file = "Sample_mp3_1.mp3"; // Replace with your input file
    int chunk_duration = 10; // Chunk size in seconds
    
     // Call the decode_data function
    std::vector<std::pair<AVFrame*, int64_t>> decoded_frames = decode_data(input_file, chunk_duration);
	for (auto& frame_pair : decoded_frames) {
    	//process_decoded_frame(frame_pair.first);
    	av_frame_free(&(frame_pair.first));
    }
    
    return 0;
}

