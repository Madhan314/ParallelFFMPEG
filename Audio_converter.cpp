#include <iostream>
#include <fstream>
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswresample/swresample.h>
    #include <libavutil/opt.h>
}

void decode_audio(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt) {
    int ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        std::cerr << "Error sending packet for decoding" << std::endl;
        return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN)) {
            return; // More input needed for decoding
        } else if (ret == AVERROR_EOF) {
            return; // End of file
        }
        if (ret < 0) {
            std::cerr << "Error during decoding" << std::endl;
            return;
        }
    }
}

void encode_audio(AVCodecContext *enc_ctx, AVFrame *frame, AVFormatContext *output_fmt_ctx) {
    int ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        std::cerr << "Error sending frame for encoding: " << std::endl;
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Could not allocate AVPacket" << std::endl;
        return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) {
            std::cerr << "Error during encoding: " << std::endl;
            av_packet_free(&pkt);
            return;
        }

        // Write the encoded packet to the output format context
        pkt->stream_index = 0; // Assuming the first stream is the audio stream
        ret = av_interleaved_write_frame(output_fmt_ctx, pkt);
        if (ret < 0) {
            std::cerr << "Error writing encoded packet to output" << std::endl;
            av_packet_unref(pkt);
            av_packet_free(&pkt);
            return;
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}

void process_audio(const std::string &input_file, const std::string &output_file) {
    AVFormatContext *fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_file.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file" << std::endl;
        return;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        avformat_close_input(&fmt_ctx);
        return;
    }

    int stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (stream_index < 0) {
        std::cerr << "Could not find audio stream in the input file" << std::endl;
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVStream *audio_stream = fmt_ctx->streams[stream_index];
    const AVCodec *decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
    if (!decoder) {
        std::cerr << "Failed to find decoder for stream" << std::endl;
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVCodecContext *dec_ctx = avcodec_alloc_context3(decoder);
    if (!dec_ctx) {
        std::cerr << "Failed to allocate the decoder context" << std::endl;
        avformat_close_input(&fmt_ctx);
        return;
    }

    if (avcodec_parameters_to_context(dec_ctx, audio_stream->codecpar) < 0) {
        std::cerr << "Failed to copy decoder parameters to input decoder context" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    if (avcodec_open2(dec_ctx, decoder, nullptr) < 0) {
        std::cerr << "Failed to open decoder" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // Ensure channel layout is set
    if (dec_ctx->channel_layout == 0) {
        dec_ctx->channel_layout = av_get_default_channel_layout(dec_ctx->channels);
    }

    const AVCodec *encoder = avcodec_find_encoder_by_name("libmp3lame");
    if (!encoder) {
        std::cerr << "Necessary encoder not found" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVCodecContext *enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        std::cerr << "Failed to allocate the encoder context" << std::endl;
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    enc_ctx->sample_rate = dec_ctx->sample_rate;
    enc_ctx->bit_rate = 128000;
    enc_ctx->channel_layout = dec_ctx->channel_layout;
    enc_ctx->channels = av_get_channel_layout_nb_channels(enc_ctx->channel_layout);
    enc_ctx->sample_fmt = encoder->sample_fmts[0];

    if (avcodec_open2(enc_ctx, encoder, nullptr) < 0) {
        std::cerr << "Failed to open encoder" << std::endl;
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // Create output format context
    AVFormatContext *output_fmt_ctx = nullptr;
    avformat_alloc_output_context2(&output_fmt_ctx, nullptr, "mp3", output_file.c_str());
    if (!output_fmt_ctx) {
        std::cerr << "Could not create output context" << std::endl;
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVStream *out_stream = avformat_new_stream(output_fmt_ctx, encoder);
    if (!out_stream) {
        std::cerr << "Failed to create new stream" << std::endl;
        avformat_free_context(output_fmt_ctx);
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    if (avcodec_parameters_from_context(out_stream->codecpar, enc_ctx) < 0) {
        std::cerr << "Failed to copy encoder parameters to output stream" << std::endl;
        avformat_free_context(output_fmt_ctx);
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    if (!(output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_fmt_ctx->pb, output_file.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            avformat_free_context(output_fmt_ctx);
            avcodec_free_context(&enc_ctx);
            avcodec_free_context(&dec_ctx);
            avformat_close_input(&fmt_ctx);
            return;
        }
    }

    if (avformat_write_header(output_fmt_ctx, nullptr) < 0) {
        std::cerr << "Error occurred when opening output file" << std::endl;
        avio_closep(&output_fmt_ctx->pb);
        avformat_free_context(output_fmt_ctx);
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // Initialize resampling context
    SwrContext *swr_ctx = swr_alloc_set_opts(nullptr,
        enc_ctx->channel_layout, enc_ctx->sample_fmt, enc_ctx->sample_rate,
        dec_ctx->channel_layout, dec_ctx->sample_fmt, dec_ctx->sample_rate,
        0, nullptr);

    if (!swr_ctx || swr_init(swr_ctx) < 0) {
        std::cerr << "Failed to initialize the resampling context" << std::endl;
        swr_free(&swr_ctx);
        avio_closep(&output_fmt_ctx->pb);
        avformat_free_context(output_fmt_ctx);
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        std::cerr << "Could not allocate audio frame" << std::endl;
        avio_closep(&output_fmt_ctx->pb);
        avformat_free_context(output_fmt_ctx);
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        swr_free(&swr_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "Could not allocate AVPacket" << std::endl;
        av_frame_free(&frame);
        avio_closep(&output_fmt_ctx->pb);
        avformat_free_context(output_fmt_ctx);
        avcodec_free_context(&enc_ctx);
        avcodec_free_context(&dec_ctx);
        swr_free(&swr_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == stream_index) {
            decode_audio(dec_ctx, frame, pkt);

            if (frame->nb_samples > 0) {
                AVFrame *resampled_frame = av_frame_alloc();
                resampled_frame->channel_layout = enc_ctx->channel_layout;
                resampled_frame->format = enc_ctx->sample_fmt;
                resampled_frame->sample_rate = enc_ctx->sample_rate;
                resampled_frame->nb_samples = frame->nb_samples;

                av_frame_get_buffer(resampled_frame, 0);

                swr_convert(swr_ctx, resampled_frame->data, resampled_frame->nb_samples,
            (const uint8_t **)frame->data, frame->nb_samples);

				// Encode the resampled frame
			encode_audio(enc_ctx, resampled_frame, output_fmt_ctx);
 

                av_frame_free(&resampled_frame);
            }
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(output_fmt_ctx);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avio_closep(&output_fmt_ctx->pb);
    avformat_free_context(output_fmt_ctx);
    avcodec_free_context(&enc_ctx);
    avcodec_free_context(&dec_ctx);
    swr_free(&swr_ctx);
    avformat_close_input(&fmt_ctx);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input file> <output file>" << std::endl;
        return 1;
    }

    std::string input_file = argv[1];
    std::string output_file = argv[2];
    process_audio(input_file, output_file);


}
