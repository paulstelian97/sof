#
# Topology for generic Icelake board with nocodec
#

# Include topology builder
include(`utils.m4')
include(`dai.m4')
include(`pipeline.m4')
include(`esai.m4')
include(`pcm.m4')

# Include TLV library
include(`common/tlv.m4')

# Include Token library
include(`sof/tokens.m4')
include(`buffer.m4')
include(`mixer.m4')
include(`platform/imx/imx8.m4')


# Low Latency playback pipeline 1 on PCM 0 using max 2 channels of s32le.
# Schedule 48 frames per 1000us deadline on core 0 with priority 0
PIPELINE_PCM_ADD(sof/pipe-volume-playback.m4,
	1, 0, 2, s32le,
	1000, 0, 0,
	8000, 96000, 48000)

# playback DAI is SSP5 using 2 periods
# Buffers use s24le format, with 48 frame per 1000us on core 0 with priority 0
DAI_ADD(sof/pipe-dai-playback.m4,
	1, ESAI, 0, NoCodec-0,
	PIPELINE_SOURCE_1, 2, s24le,
	1000, 0, 0)

# PCM Low Latency, id 0
PCM_PLAYBACK_ADD(Port5, 0, PIPELINE_PCM_1)


DAI_CONFIG(ESAI, 0, 0, NoCodec-0,
    ESAI_CONFIG(I2S, ESAI_CLOCK(mclk, 24576000, codec_mclk_in),
    ESAI_CLOCK(bclk, 3072000, codec_slave),
    ESAI_CLOCK(fsync, 48000, codec_slave),
    ESAI_TDM(2, 32, 3, 3),
    ESAI_CONFIG_DATA(ESAI, 0, 24)))






