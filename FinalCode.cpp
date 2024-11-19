#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <algorithm>
#include <chrono>
#include <omp.h>
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
    #include <libavutil/opt.h>
    #include <libavutil/channel_layout.h>
    #include <libavutil/samplefmt.h>
}

using namespace std;
std::mutex mtx;
std::condition_variable cv;
std::queue<AVFrame*> frame_queue; // Queue to hold frames for encoding
bool finished = false;

// Process audio frame using OpenMP for parallel processing
void process_audio_frame(AVFrame* frame, enum AVSampleFormat format) {
    // Simple volume adjustment (increase volume by 10%)
    /*omp_set_num_threads(2);
    #pragma omp parallel sections
    {
    	#pragma omp section 
    	{*/
    	for (int i = 0; i < frame->nb_samples/2; i++) {
        	for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
        	    switch (format) {
        	        case AV_SAMPLE_FMT_FLT:
        	        case AV_SAMPLE_FMT_FLTP: {
        	            float* sample = (float*)frame->data[ch] + i;
        	            *sample *= 1.8f;
        	            *sample = std::clamp(*sample, -1.0f, 1.0f);
        	            break;
        	        }
        	        case AV_SAMPLE_FMT_S16:
        	        case AV_SAMPLE_FMT_S16P: {
        	            int16_t* sample = (int16_t*)frame->data[ch] + i;
        	            int32_t temp = *sample * 1.8;
        	            *sample = std::clamp(temp, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
        	            break;
        	        }
        	        case AV_SAMPLE_FMT_S32:
        	        case AV_SAMPLE_FMT_S32P: {
        	            int32_t* sample = (int32_t*)frame->data[ch] + i;
        	            int64_t temp = *sample * 1.8;
        	            *sample = std::clamp(temp, (int64_t)INT32_MIN, (int64_t)INT32_MAX);
        	            break;
        	        }
        	        default:
        	            break;
        	    }
        	}
    	}
    	//}
    	
    	/*#pragma omp section 
    	{*/
    	for (int i = frame->nb_samples/2; i < frame->nb_samples; i++) {
        	for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
        	    switch (format) {
        	        case AV_SAMPLE_FMT_FLT:
        	        case AV_SAMPLE_FMT_FLTP: {
        	            float* sample = (float*)frame->data[ch] + i;
        	            *sample *= 1.8f;
        	            *sample = std::clamp(*sample, -1.0f, 1.0f);
        	            break;
        	        }
        	        case AV_SAMPLE_FMT_S16:
        	        case AV_SAMPLE_FMT_S16P: {
        	            int16_t* sample = (int16_t*)frame->data[ch] + i;
        	            int32_t temp = *sample * 1.8;
        	            *sample = std::clamp(temp, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
        	            break;
        	        }
        	        case AV_SAMPLE_FMT_S32:
        	        case AV_SAMPLE_FMT_S32P: {
        	            int32_t* sample = (int32_t*)frame->data[ch] + i;
        	            int64_t temp = *sample * 1.8;
        	            *sample = std::clamp(temp, (int64_t)INT32_MIN, (int64_t)INT32_MAX);
        	            break;
        	        }
        	        default:
        	            break;
        	    }
        	}
    	}
    	//}
    //}
}


void encoder_thread(AVCodecContext *encoder_ctx, AVFormatContext *output_format_ctx) {
    while (true) {
        AVFrame* frame;

        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [] { return !frame_queue.empty() || finished; });

            if (!frame_queue.empty()) {
                frame = frame_queue.front();
                frame_queue.pop();
            } else {
                break; // Exit if no more frames and finished is true
            }
        }

        // Send frame to encoder
        if (avcodec_send_frame(encoder_ctx, frame) < 0) {
            std::cerr << "Error sending frame to encoder" << std::endl;
            break;
        }

        AVPacket *output_packet = av_packet_alloc();
        while (avcodec_receive_packet(encoder_ctx, output_packet) == 0) {
            output_packet->stream_index = 0;
            av_packet_rescale_ts(output_packet, encoder_ctx->time_base, output_format_ctx->streams[0]->time_base);
            if (av_interleaved_write_frame(output_format_ctx, output_packet) < 0) {
                std::cerr << "Error writing output packet" << std::endl;
                break;
            }
        }
        av_packet_free(&output_packet);
        av_frame_free(&frame); // Free the frame after processing
    }
}

int main(int argc, char *argv[]) {

	auto start = std::chrono::high_resolution_clock::now();
	
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file>" << std::endl;
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = argv[2];

    // Initialize FFmpeg
    av_log_set_level(AV_LOG_VERBOSE);

    AVFormatContext *input_format_ctx = nullptr;
    if (avformat_open_input(&input_format_ctx, input_file, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file '" << input_file << "'" << std::endl;
        return 1;
    }

    if (avformat_find_stream_info(input_format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    // Find the audio stream
    int audio_stream_index = -1;
    AVCodecParameters *audio_codecpar = nullptr;
    for (unsigned int i = 0; i < input_format_ctx->nb_streams; i++) {
        if (input_format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            audio_codecpar = input_format_ctx->streams[i]->codecpar;
            break;
        }
    }

    if (audio_stream_index == -1) {
        std::cerr << "Could not find audio stream in input file" << std::endl;
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    // Initialize the decoder
    const AVCodec *decoder = avcodec_find_decoder(audio_codecpar->codec_id);
    AVCodecContext *decoder_ctx = avcodec_alloc_context3(decoder);
    if (!decoder_ctx) {
        std::cerr << "Could not allocate decoder context" << std::endl;
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    if (avcodec_parameters_to_context(decoder_ctx, audio_codecpar) < 0) {
        std::cerr << "Could not copy codec parameters to decoder context" << std::endl;
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    if (avcodec_open2(decoder_ctx, decoder, nullptr) < 0) {
        std::cerr << "Could not open decoder" << std::endl;
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    // Initialize the encoder
    const AVCodec *encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    AVCodecContext *encoder_ctx = avcodec_alloc_context3(encoder);
    if (!encoder_ctx) {
        std::cerr << "Could not allocate encoder context" << std::endl;
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    encoder_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    encoder_ctx->sample_rate = decoder_ctx->sample_rate;
    encoder_ctx->ch_layout = decoder_ctx->ch_layout;
    encoder_ctx->bit_rate = 192000;

    if (avcodec_open2(encoder_ctx, encoder, nullptr) < 0) {
        std::cerr << "Could not open encoder" << std::endl;
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    // Initialize the output file
    AVFormatContext *output_format_ctx = nullptr;
    if (avformat_alloc_output_context2(&output_format_ctx, nullptr, nullptr, output_file) < 0) {
        std::cerr << "Could not create output context" << std::endl;
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    AVStream *out_stream = avformat_new_stream(output_format_ctx, nullptr);
    if (!out_stream) {
        std::cerr << "Failed to allocate output stream" << std::endl;
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    if (avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx) < 0) {
        std::cerr << "Failed to copy encoder parameters to output stream" << std::endl;
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_format_ctx->pb, output_file, AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file '" << output_file << "'" << std::endl;
            avformat_free_context(output_format_ctx);
            avcodec_free_context(&encoder_ctx);
            avcodec_free_context(&decoder_ctx);
            avformat_close_input(&input_format_ctx);
            return 1;
        }
    }

    if (avformat_write_header(output_format_ctx, nullptr) < 0) {
        std::cerr << "Error occurred when opening output file" << std::endl;
        avformat_free_context(output_format_ctx);
        avcodec_free_context(&encoder_ctx);
        avcodec_free_context(&decoder_ctx);
        avformat_close_input(&input_format_ctx);
        return 1;
    }

    
    
    SwrContext *swr_ctx = swr_alloc();
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &decoder_ctx->ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", decoder_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", decoder_ctx->sample_fmt, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &encoder_ctx->ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", encoder_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", encoder_ctx->sample_fmt, 0);
    swr_init(swr_ctx);

    AVPacket *input_packet = av_packet_alloc();
    AVFrame *input_frame = av_frame_alloc();
    AVFrame *resampled_frame = av_frame_alloc();
    resampled_frame->nb_samples = encoder_ctx->frame_size;
    resampled_frame->format = encoder_ctx->sample_fmt;
    resampled_frame->ch_layout = encoder_ctx->ch_layout;
    av_frame_get_buffer(resampled_frame, 0);

    
	
	omp_set_num_threads(2); // Use 2 threads: 1 for decoding and 1 for encoding

    #pragma omp parallel
    {
        if (omp_get_thread_num() == 0) {
            // Producer: Read and decode frames
            while (av_read_frame(input_format_ctx, input_packet) >= 0) {
                if (input_packet->stream_index == audio_stream_index) {
                    if (avcodec_send_packet(decoder_ctx, input_packet) < 0) {
                        std::cerr << "Error submitting packet for decoding" << std::endl;
                        break;
                    }

                    AVFrame* input_frame = av_frame_alloc();
                    while (avcodec_receive_frame(decoder_ctx, input_frame) == 0) {
                        // Process audio in its original format (optional)
                        process_audio_frame(input_frame, decoder_ctx->sample_fmt);
                        
                        #pragma omp critical
                       {
                            std::lock_guard<std::mutex> lock(mtx);
                            frame_queue.push(av_frame_clone(input_frame)); // Clone frame for safe access
                        }
                        cv.notify_one(); // Notify the encoder thread
                    }
                    av_frame_free(&input_frame);
                }
                av_packet_unref(input_packet);
            }

            // Signal that no more frames will be added
            {
                std::lock_guard<std::mutex> lock(mtx);
                finished = true;
            }
            cv.notify_one(); // Notify the encoder thread to check for finished state
        } else {
            // Consumer: Encode and write frames
            encoder_thread(encoder_ctx, output_format_ctx);
        }
    }
    
    // Flush encoder
    avcodec_send_frame(encoder_ctx, nullptr);
    AVPacket *output_packet = av_packet_alloc();
    while (avcodec_receive_packet(encoder_ctx, output_packet) == 0) {
        output_packet->stream_index = 0;
        av_packet_rescale_ts(output_packet, encoder_ctx->time_base, out_stream->time_base);
        if (av_interleaved_write_frame(output_format_ctx, output_packet) < 0) {
            std::cerr << "Error writing output packet during flushing" << std::endl;
            break;
        }
    }
    av_packet_free(&output_packet);

    av_write_trailer(output_format_ctx);

    // Clean up
    swr_free(&swr_ctx);
    av_frame_free(&input_frame);
    av_frame_free(&resampled_frame);
    av_packet_free(&input_packet);
    avcodec_free_context(&decoder_ctx);
    avcodec_free_context(&encoder_ctx);
    avformat_close_input(&input_format_ctx);
    if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_ctx->pb);
    avformat_free_context(output_format_ctx);

    std::cout << "Audio processing completed successfully." << std::endl;
    
    auto end = std::chrono::high_resolution_clock::now();

    // Calculate the duration in microseconds (or any other unit)
    std::chrono::duration<double> execution_time = end - start;
    
    // Display execution time
    std::cout << "Execution time: " << execution_time.count() << " seconds" << std::endl;

    return 0;
}
