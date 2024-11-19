#include <iostream>
#include <vector>
#include <string>
#include <fstream>
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
//equaliser to  enhance clarity, bass, or treble as desired. 
void apply_equalizer(AVFrame* frame, enum AVSampleFormat format, float bass_gain, float treble_gain) {
    
    #pragma omp parallel for schedule(static, 288)
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    if (*sample < 100.0f && *sample>=50.0f) {  // Apply bass gain for low frequencies
                        *sample *= bass_gain;
                    } 
                    else if (*sample > 5000.0f) {  // Apply treble gain for high frequencies
                        *sample *= treble_gain;
                    }
                    *sample = std::clamp(*sample, -1.0f, 1.0f);
                    break;
                }
                default:
                    break;
            }
        }
    }
}
//Compression reduces the volume of louder sounds and increases the volume of quieter sounds, which can help maintain consistency and make softer sounds more audible.
void apply_compression(AVFrame* frame, enum AVSampleFormat format, float threshold, float ratio) {
	
    #pragma omp parallel for schedule(static, 288)
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    if (std::abs(*sample) > threshold) {
                        *sample = threshold + (*sample - threshold) / ratio;
                    }
                    *sample = std::clamp(*sample, -1.0f, 1.0f);
                    break;
                }
                default:
                    break;
            }
        }
    }
}
//Adding a subtle reverb can enhance the audio's natural quality
void apply_reverb(AVFrame* frame, enum AVSampleFormat format, float decay_factor, int delay_samples) {
    
    #pragma omp parallel for schedule(static, 288)
    for (int i = delay_samples; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* current_sample = (float*)frame->data[ch] + i;
                    float* delayed_sample = (float*)frame->data[ch] + i - delay_samples;
                    *current_sample += (*delayed_sample) * decay_factor;
                    *current_sample = std::clamp(*current_sample, -1.0f, 1.0f);
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void apply_bandpass_filter(AVFrame* frame, enum AVSampleFormat format, float low_cutoff, float high_cutoff) {
    
    #pragma omp parallel for schedule(static, 288)
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;

                    // Frequencies below low_cutoff are reduced (bass cut)
                    if (*sample < low_cutoff) {
                        *sample *= 0.1f; // Reduce bass frequencies drastically
                    }
                    
                    // Frequencies above high_cutoff are reduced (treble cut)
                    else if (*sample > high_cutoff) {
                        *sample *= 0.1f; // Reduce high frequencies drastically
                    }
                    
                    // Clamp values to avoid distortion
                    *sample = std::clamp(*sample, -1.0f, 1.0f);
                    break;
                }
                default:
                    break;
            }
        }
    }
}

//adjust volume
void adjust_volume(AVFrame* frame, enum AVSampleFormat format, float gain){
    
    #pragma omp parallel for schedule(static, 288)
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            // Adjust volume
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    *sample *= gain;
                    *sample = std::clamp(*sample, -1.0f, 1.0f);
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    int32_t temp = *sample * gain;
                    *sample = std::clamp(temp, (int32_t)INT16_MIN, (int32_t)INT16_MAX);
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    int64_t temp = *sample * gain;
                    *sample = std::clamp(temp, (int64_t)INT32_MIN, (int64_t)INT32_MAX);
                    break;
                }
                default:
                    break;
            }
        }
    }
}

void noise_reduction(AVFrame* frame, enum AVSampleFormat format, float noise_threshold, int window_size) {
    // Ensure the window size is odd to center the average around the current sample
    if (window_size % 2 == 0) {
        window_size++;
    }
    
    int half_window = window_size / 2;
    
    #pragma omp parallel for simd
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            float smoothed_sample = 0.0f;
            int count = 0;

            // Gather samples from the window
            for (int j = -half_window; j <= half_window; j++) {
                int index = i + j;
                if (index >= 0 && index < frame->nb_samples) {
                    switch (format) {
                        case AV_SAMPLE_FMT_FLT:
                        case AV_SAMPLE_FMT_FLTP: {
                            float* sample = (float*)frame->data[ch] + index;
                            smoothed_sample += *sample;
                            count++;
                            break;
                        }
                        case AV_SAMPLE_FMT_S16:
                        case AV_SAMPLE_FMT_S16P: {
                            int16_t* sample = (int16_t*)frame->data[ch] + index;
                            smoothed_sample += static_cast<float>(*sample);
                            count++;
                            break;
                        }
                        case AV_SAMPLE_FMT_S32:
                        case AV_SAMPLE_FMT_S32P: {
                            int32_t* sample = (int32_t*)frame->data[ch] + index;
                            smoothed_sample += static_cast<float>(*sample);
                            count++;
                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            // Average the collected samples
            if (count > 0) {
                smoothed_sample /= count;
            }

            // Apply noise reduction based on the noise threshold
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    if (std::fabs(smoothed_sample) < noise_threshold) {
                        *sample = 0.0f; // Set to zero if below threshold
                    } else {
                        *sample = smoothed_sample; // Set to smoothed value
                    }
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    if (std::abs(static_cast<int16_t>(smoothed_sample)) < static_cast<int16_t>(noise_threshold * INT16_MAX)) {
                        *sample = 0; // Set to zero if below threshold
                    } else {
                        *sample = std::clamp(static_cast<int16_t>(smoothed_sample), static_cast<int16_t>(INT16_MIN), static_cast<int16_t>(INT16_MAX)); // Clamp to valid range
                    }
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    if (std::abs(static_cast<int32_t>(smoothed_sample)) < static_cast<int32_t>(noise_threshold * INT32_MAX)) {
                        *sample = 0; // Set to zero if below threshold
                    } else {
                        *sample = std::clamp(static_cast<int32_t>(smoothed_sample), static_cast<int32_t>(INT32_MIN), static_cast<int32_t>(INT32_MAX)); // Clamp to valid range
                    }
                    break;
                }
                default:
                    break;
            }
        }
    }
}




void normalize_audio(AVFrame* frame, enum AVSampleFormat format, float target_level) {
    float max_sample_value = 0.0f;

    // First pass: find the maximum sample value
    #pragma omp parallel for reduction(max:max_sample_value)
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    max_sample_value = std::max(max_sample_value, std::fabs(*sample));
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    max_sample_value = std::max(max_sample_value, std::fabs(static_cast<float>(*sample))); // Cast to float for comparison
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    max_sample_value = std::max(max_sample_value, std::fabs(static_cast<float>(*sample))); // Cast to float for comparison
                    break;
                }
                default:
                    break;
            }
        }
    }

    // Calculate the normalization factor
    float normalization_factor = (max_sample_value > 0) ? target_level / max_sample_value : 1.0f;

    // Second pass: normalize samples
    #pragma omp parallel for
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    *sample *= normalization_factor; // Scale sample
                    *sample = std::clamp(*sample, -1.0f, 1.0f); // Clamp to float range
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    *sample = std::clamp(static_cast<int16_t>(*sample * normalization_factor), 
                                         static_cast<int16_t>(INT16_MIN), 
                                         static_cast<int16_t>(INT16_MAX)); // Scale sample
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    *sample = std::clamp(static_cast<int32_t>(*sample * normalization_factor), 
                                         static_cast<int32_t>(INT32_MIN), 
                                         static_cast<int32_t>(INT32_MAX)); // Scale sample
                    break;
                }
                default:
                    break;
            }
        }
    }
}


// Process audio frame using OpenMP for parallel processing
void process_audio_frame(AVFrame* frame, enum AVSampleFormat format) {
    float bass_gain = 1.05f;  // Adjust bass gain
    float treble_gain = 1.08f;  // Adjust treble gain
    float compression_threshold = 0.7f;  // Adjust threshold
    float compression_ratio = 2.0f;  // Adjust higher ratio
    float decay_factor = 0.1f;  // decay for subtle reverb
    int delay_samples = 441;  // Reverb delay
    float volume_gain = 1.2f;  //  volume gain
    float noise_threshold = 0.01f;  // Adjust noise threshold 
    float low_cutoff = 500.0f;  // Cut off low bands
    float high_cutoff = 15000.0f;  // Cut off highs
    int window = 5; //smoothening factor
	
	omp_set_num_threads(2);
	
	#pragma omp parallel
	{
		#pragma omp single
		{
		    #pragma omp task
		    {
		    apply_bandpass_filter(frame,format,low_cutoff,high_cutoff);
    		adjust_volume(frame, format, volume_gain); 
			}
		    #pragma omp task
		    {
		    apply_compression(frame, format, compression_threshold, compression_ratio);    
   			adjust_volume(frame, format, 1.8f);  
			}
		    #pragma omp task
		    {
		    apply_equalizer(frame, format, bass_gain, treble_gain);      
   			adjust_volume(frame, format, volume_gain);
   			}
		    #pragma omp task
		    {
		    apply_reverb(frame, format, decay_factor, delay_samples);
			adjust_volume(frame, format, 2.2f);  
		    }
		}
	}
   
}

// Decode audio frames
int decode_audio(AVFormatContext* input_format_ctx, AVCodecContext* decoder_ctx, int audio_stream_index, SwrContext* swr_ctx, AVCodecContext* encoder_ctx, AVStream* out_stream, AVFormatContext* output_format_ctx) {
    AVPacket* input_packet = av_packet_alloc();
    AVFrame* input_frame = av_frame_alloc();
    AVFrame* resampled_frame = av_frame_alloc();
    resampled_frame->nb_samples = encoder_ctx->frame_size;
    resampled_frame->format = encoder_ctx->sample_fmt;
    resampled_frame->ch_layout = encoder_ctx->ch_layout;
    av_frame_get_buffer(resampled_frame, 0);

    int64_t pts = 0;

    while (av_read_frame(input_format_ctx, input_packet) >= 0) {
        if (input_packet->stream_index == audio_stream_index) {
            if (avcodec_send_packet(decoder_ctx, input_packet) < 0) {
                std::cerr << "Error submitting packet for decoding" << std::endl;
                return 1;
            }

            while (avcodec_receive_frame(decoder_ctx, input_frame) == 0) {
                process_audio_frame(input_frame, decoder_ctx->sample_fmt); // Process frame

                // Resample
                swr_convert(swr_ctx, resampled_frame->data, resampled_frame->nb_samples,
                            (const uint8_t**)input_frame->data, input_frame->nb_samples);

                resampled_frame->pts = pts;
                pts += resampled_frame->nb_samples;

                if (avcodec_send_frame(encoder_ctx, resampled_frame) < 0) {
                    std::cerr << "Error sending frame to encoder" << std::endl;
                    return 1;
                }

                AVPacket* output_packet = av_packet_alloc();
                while (avcodec_receive_packet(encoder_ctx, output_packet) == 0) {
                    output_packet->stream_index = 0;
                    av_packet_rescale_ts(output_packet, encoder_ctx->time_base, out_stream->time_base);
                    if (av_interleaved_write_frame(output_format_ctx, output_packet) < 0) {
                        std::cerr << "Error writing output packet" << std::endl;
                        return 1;
                    }
                }
                av_packet_free(&output_packet);
            }
        }
        av_packet_unref(input_packet);
    }

    av_packet_free(&input_packet);
    av_frame_free(&input_frame);
    av_frame_free(&resampled_frame);

    return 0;
}

// Encode audio frames and finalize the output file
void encode_audio(AVCodecContext* encoder_ctx, AVStream* out_stream, AVFormatContext* output_format_ctx) {
    // Flush encoder
    avcodec_send_frame(encoder_ctx, nullptr);
    AVPacket* output_packet = av_packet_alloc();
    while (avcodec_receive_packet(encoder_ctx, output_packet) == 0) {
        output_packet->stream_index = 0;
        av_packet_rescale_ts(output_packet, encoder_ctx->time_base, out_stream->time_base);
        av_interleaved_write_frame(output_format_ctx, output_packet);
    }
    av_packet_free(&output_packet);
}

int main(int argc, char* argv[]) {
    auto start = std::chrono::high_resolution_clock::now();

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <output_file>" << std::endl;
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = argv[2];

    av_log_set_level(AV_LOG_VERBOSE);

    // Initialize input format context
    AVFormatContext* input_format_ctx = nullptr;
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
    AVCodecParameters* audio_codecpar = nullptr;
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

    // Initialize decoder
    const AVCodec* decoder = avcodec_find_decoder(audio_codecpar->codec_id);
    AVCodecContext* decoder_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_ctx, audio_codecpar);
    avcodec_open2(decoder_ctx, decoder, nullptr);

    // Initialize encoder
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MP3);
    AVCodecContext* encoder_ctx = avcodec_alloc_context3(encoder);
    encoder_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    encoder_ctx->sample_rate = decoder_ctx->sample_rate;
    encoder_ctx->ch_layout = decoder_ctx->ch_layout;
    encoder_ctx->bit_rate = 192000;
    avcodec_open2(encoder_ctx, encoder, nullptr);

    // Initialize output format context
    AVFormatContext* output_format_ctx = nullptr;
    avformat_alloc_output_context2(&output_format_ctx, nullptr, nullptr, output_file);

    AVStream* out_stream = avformat_new_stream(output_format_ctx, nullptr);
    avcodec_parameters_from_context(out_stream->codecpar, encoder_ctx);

    if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_open(&output_format_ctx->pb, output_file, AVIO_FLAG_WRITE);
    }

    if(avformat_write_header(output_format_ctx, nullptr)<0){
    	std::cout<<"Header writing issue"<<std::endl;
    };

    // Set up resampler
    SwrContext* swr_ctx = swr_alloc();
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &decoder_ctx->ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", decoder_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", decoder_ctx->sample_fmt, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &encoder_ctx->ch_layout, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", encoder_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", encoder_ctx->sample_fmt, 0);
    swr_init(swr_ctx);

    // Decode, process, and encode audio
    decode_audio(input_format_ctx, decoder_ctx, audio_stream_index, swr_ctx, encoder_ctx, out_stream, output_format_ctx);

    // Encode remaining audio frames
    encode_audio(encoder_ctx, out_stream, output_format_ctx);

    av_write_trailer(output_format_ctx);

    // Clean up
    swr_free(&swr_ctx);
    avcodec_free_context(&decoder_ctx);
    avcodec_free_context(&encoder_ctx);
    avformat_close_input(&input_format_ctx);
    if (!(output_format_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_format_ctx->pb);
    }
    avformat_free_context(output_format_ctx);

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> execution_time = end - start;
    std::cout << "Execution time: " << execution_time.count() << " seconds" << std::endl;

}
