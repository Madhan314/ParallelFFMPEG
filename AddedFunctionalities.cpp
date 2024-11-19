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
                    else{
                    	*sample *=3.0f; // Reduce high frequencies drastically
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

/*void noise_reduction(AVFrame* frame, enum AVSampleFormat format, float noise_threshold, int window_size) {
    if (window_size % 2 == 0) {
        window_size++;
    }
    
    int half_window = window_size / 2;

    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            float smoothed_sample = 0.0f;
            float weight_sum = 0.0f;
            int count = 0;

            for (int j = -half_window; j <= half_window; j++) {
                int index = i + j;
                if (index >= 0 && index < frame->nb_samples) {
                    float current_sample = 0.0f;
                    switch (format) {
                        case AV_SAMPLE_FMT_FLT:
                        case AV_SAMPLE_FMT_FLTP: {
                            float* sample = (float*)frame->data[ch] + index;
                            current_sample = *sample;
                            break;
                        }
                        case AV_SAMPLE_FMT_S16:
                        case AV_SAMPLE_FMT_S16P: {
                            int16_t* sample = (int16_t*)frame->data[ch] + index;
                            current_sample = static_cast<float>(*sample);
                            break;
                        }
                        case AV_SAMPLE_FMT_S32:
                        case AV_SAMPLE_FMT_S32P: {
                            int32_t* sample = (int32_t*)frame->data[ch] + index;
                            current_sample = static_cast<float>(*sample);
                            break;
                        }
                        default:
                            break;
                    }

                    // Calculate weight based on distance from the current sample
                    float weight = 1.0f - (fabs((float)j) / half_window);
                    smoothed_sample += current_sample * weight;
                    weight_sum += weight;
                    count++;
                }
            }

            if (count > 0) {
                smoothed_sample /= weight_sum;
            }

            // Adaptive noise thresholding
            float adaptive_threshold = noise_threshold * (1.0f + 0.5f * (1.0f - fabs(smoothed_sample)));

            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < adaptive_threshold) ? 0.0f : smoothed_sample;
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < adaptive_threshold * INT16_MAX) ? 0 : std::clamp(static_cast<int16_t>(smoothed_sample), static_cast<int16_t>(INT16_MIN), static_cast<int16_t>(INT16_MAX));
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < adaptive_threshold * INT32_MAX) ? 0 : std::clamp(static_cast<int32_t>(smoothed_sample), static_cast<int32_t>(INT32_MIN), static_cast<int32_t>(INT32_MAX));
                    break;
                }
                default:
                    break;
            }
        }
    }
}*/

void noise_reduction(AVFrame* frame, enum AVSampleFormat format, float noise_threshold, int window_size) {
    if (window_size % 2 == 0) {
        window_size++;
    }

    int half_window = window_size / 2;

    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            float smoothed_sample = 0.0f;
            float weight_sum = 0.0f;
            int count = 0;

            for (int j = -half_window; j <= half_window; j++) {
                int index = i + j;
                if (index >= 0 && index < frame->nb_samples) {
                    float current_sample = 0.0f;
                    switch (format) {
                        case AV_SAMPLE_FMT_FLT:
                        case AV_SAMPLE_FMT_FLTP: {
                            float* sample = (float*)frame->data[ch] + index;
                            current_sample = *sample ;
                            break;
                        }
                        case AV_SAMPLE_FMT_S16:
                        case AV_SAMPLE_FMT_S16P: {
                            int16_t* sample = (int16_t*)frame->data[ch] + index;
                            current_sample = (float)(*sample) / INT16_MAX;
                            break;
                        }
                        case AV_SAMPLE_FMT_S32:
                        case AV_SAMPLE_FMT_S32P: {
                            int32_t* sample = (int32_t*)frame->data[ch] + index;
                            current_sample = (float)(*sample) / INT32_MAX;
                            break;
                        }
                        default:
                            break;
                    }

                    // Gaussian weight based on distance from the current sample
                    float weight = expf(-fabs((float)j) / half_window);
                    smoothed_sample += current_sample * weight*0.7;
                    weight_sum += (weight*0.7);
                    count++;
                }
            }

            if (count > 0) {
                smoothed_sample /= weight_sum;
            }

            // Adaptive noise thresholding
            float adaptive_threshold = noise_threshold * (1.0f + 0.1f * (1.0f - fabs(smoothed_sample)));

          
            
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < adaptive_threshold) ? 0.0f : fmaxf(-0.7f, fminf(smoothed_sample, 0.7f));
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < adaptive_threshold) ? 0 : (int16_t)fmaxf(INT16_MIN*0.7f, fminf(smoothed_sample * (INT16_MAX)*0.7f, INT16_MAX*0.7f));
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < adaptive_threshold) ? 0 : (int32_t)fmaxf(INT32_MIN*0.7f, fminf(smoothed_sample * (INT32_MAX*0.7f), INT32_MAX*0.7f));
                    break;
                }
                default:
                    break;
            }
            
        }
    }
}

void mute_silent_sections(AVFrame* frame, enum AVSampleFormat format, float silence_threshold, int window_size) {
    if (window_size % 2 == 0) {
        window_size++;
    }

    int half_window = window_size / 2;

    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            float smoothed_sample = 0.0f;
            float weight_sum = 0.0f;
            int silent_count = 0;

            for (int j = -half_window; j <= half_window; j++) {
                int index = i + j;
                if (index >= 0 && index < frame->nb_samples) {
                    float current_sample = 0.0f;
                    switch (format) {
                        case AV_SAMPLE_FMT_FLT:
                        case AV_SAMPLE_FMT_FLTP: {
                            float* sample = (float*)frame->data[ch] + index;
                            current_sample = *sample;
                            break;
                        }
                        case AV_SAMPLE_FMT_S16:
                        case AV_SAMPLE_FMT_S16P: {
                            int16_t* sample = (int16_t*)frame->data[ch] + index;
                            current_sample = (float)(*sample) / INT16_MAX;
                            break;
                        }
                        case AV_SAMPLE_FMT_S32:
                        case AV_SAMPLE_FMT_S32P: {
                            int32_t* sample = (int32_t*)frame->data[ch] + index;
                            current_sample = (float)(*sample) / INT32_MAX;
                            break;
                        }
                        default:
                            break;
                    }

                    float weight = 1.0f - (fabs((float)j) / half_window);
                    smoothed_sample += current_sample * weight;
                    weight_sum += weight;

                    // Count silent samples (below the silence threshold)
                    if (fabs(current_sample) < silence_threshold) {
                        silent_count++;
                    }
                }
            }

            if (weight_sum > 0) {
                smoothed_sample /= weight_sum;
            }

            // Check if majority of the samples in the window are silent
            if (silent_count > half_window) {
                // Mute this sample if most samples in the window are silent
                smoothed_sample = 0.0f;
            }

            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < silence_threshold) ? 0.0f : fmaxf(-0.8f, fminf(smoothed_sample, 0.8f));
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < silence_threshold) ? 0 : (int16_t)fmaxf(INT16_MIN+2048, fminf(smoothed_sample * INT16_MAX, INT16_MAX-2048));
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    *sample = (fabs(smoothed_sample) < silence_threshold) ? 0 : (int32_t)fmaxf(INT32_MIN+32256, fminf(smoothed_sample * INT32_MAX, INT32_MAX-32256));
                    break;
                }
                default:
                    break;
            }
        }
    }
}


/*
void normalize_audio(AVFrame* frame, enum AVSampleFormat format, float target_level) {
    float max_sample_value = 0.0f;

    // First pass: find the maximum sample value
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
                    max_sample_value = std::max(max_sample_value, std::fabs(static_cast<float>(*sample)));
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    max_sample_value = std::max(max_sample_value, std::fabs(static_cast<float>(*sample)));
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
                    float scaled_sample = static_cast<float>(*sample) * normalization_factor;
                    *sample = std::clamp(static_cast<int16_t>(scaled_sample), static_cast<int16_t>(INT16_MIN),static_cast<int16_t>(INT16_MAX));
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    float scaled_sample = static_cast<float>(*sample) * normalization_factor;
                    *sample = std::clamp(static_cast<int32_t>(scaled_sample), static_cast<int32_t>(INT32_MIN),static_cast<int32_t>(INT32_MAX));
                    break;
                }
                default:
                    break;
            }
        }
    }
}*/

void normalize_audio_with_noise_gate(AVFrame* frame, enum AVSampleFormat format, float target_level, float noise_gate_threshold) {
    float max_sample_value = 0.0f;

    // First pass: find the maximum sample value and apply noise gate
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    // Apply noise gate
                    if (fabs(*sample) < noise_gate_threshold) {
                        *sample = 0.0f;  // Mute if below threshold
                    } else {
                        max_sample_value = std::max(max_sample_value, std::fabs(*sample));
                    }
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    // Apply noise gate
                    if (fabs(static_cast<float>(*sample)) < noise_gate_threshold) {
                        *sample = 0;  // Mute if below threshold
                    } else {
                        max_sample_value = std::max(max_sample_value, std::fabs(static_cast<float>(*sample)));
                    }
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    // Apply noise gate
                    if (fabs(static_cast<float>(*sample)) < noise_gate_threshold) {
                        *sample = 0;  // Mute if below threshold
                    } else {
                        max_sample_value = std::max(max_sample_value, std::fabs(static_cast<float>(*sample)));
                    }
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
                    float scaled_sample = static_cast<float>(*sample) * normalization_factor;
                    *sample = std::clamp(static_cast<int16_t>(scaled_sample), static_cast<int16_t>(INT16_MIN), static_cast<int16_t>(INT16_MAX));
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    float scaled_sample = static_cast<float>(*sample) * normalization_factor;
                    *sample = std::clamp(static_cast<int32_t>(scaled_sample), static_cast<int32_t>(INT32_MIN), static_cast<int32_t>(INT32_MAX));
                    break;
                }
                default:
                    break;
            }
        }
    }
}


float calculate_average_noise_level(AVFrame* frame, enum AVSampleFormat format) {
    float total_noise = 0.0f;
    int sample_count = 0;

    for (int i = 0; i < frame->nb_samples; i++) {
        for (int ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
            switch (format) {
                case AV_SAMPLE_FMT_FLT:
                case AV_SAMPLE_FMT_FLTP: {
                    float* sample = (float*)frame->data[ch] + i;
                    total_noise += std::fabs(*sample);
                    sample_count++;
                    break;
                }
                case AV_SAMPLE_FMT_S16:
                case AV_SAMPLE_FMT_S16P: {
                    int16_t* sample = (int16_t*)frame->data[ch] + i;
                    total_noise += std::fabs(static_cast<float>(*sample));
                    sample_count++;
                    break;
                }
                case AV_SAMPLE_FMT_S32:
                case AV_SAMPLE_FMT_S32P: {
                    int32_t* sample = (int32_t*)frame->data[ch] + i;
                    total_noise += std::fabs(static_cast<float>(*sample));
                    sample_count++;
                    break;
                }
                default:
                    break;
            }
        }
    }

    return (sample_count > 0) ? total_noise / sample_count : 0.0f;
}


// Process audio frame using OpenMP for parallel processing
void process_audio_frame(AVFrame* frame, enum AVSampleFormat format) {
    float bass_gain = 1.01f;  // Adjust bass gain
    float treble_gain = 1.02f;  // Adjust treble gain
    float compression_threshold = 0.7f;  // Adjust threshold
    float compression_ratio = 2.0f;  // Adjust higher ratio
    float decay_factor = 0.1f;  // decay for subtle reverb
    int delay_samples = 441;  // Reverb delay
    float volume_gain = 2.0f;  //  volume gain
    float noise_threshold = 0.01f;  // Adjust noise threshold 
    float low_cutoff = 14000.0f;  // Cut off low bands
    float high_cutoff = 17000.0f;  // Cut off highs
    int window = 10; //smoothening factor
	float target_level = 0.1f; 
	float noise_reduction_multiplier = 1.4f;
	float silence_threshold = 0.2f;
	
	//mute_silent_sections(frame,format,silence_threshold, window);
	apply_bandpass_filter(frame,format,low_cutoff,high_cutoff);
	apply_compression(frame, format, compression_threshold, compression_ratio);
	
	adjust_volume(frame, format, volume_gain);
   	adjust_volume(frame, format, volume_gain);
    
    apply_equalizer(frame, format, bass_gain, treble_gain);
   	
    apply_reverb(frame, format, decay_factor, delay_samples);
    noise_reduction(frame, format, noise_threshold,window);
    //adjust_volume(frame, format, volume_gain);
    
 
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
