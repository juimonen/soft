/*
 * Copyright (c) 2018, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <uapi/sound/asoc.h>
#include <uapi/sound/sof-ipc.h>
#include <uapi/sound/sof-topology.h>

#define PIPE_MIN_DEADLINE 1
#define PIPE_MAX_DEADLINE 1000
#define PIPE_MIN_PRIORITY 1
#define PIPE_MAX_PRIORITY 1000
#define PIPE_MIN_MIPS 1
#define PIPE_MAX_MIPS 1000
#define PIPE_MIN_CORE 1
#define PIPE_MAX_CORE 1000
#define PIPE_MIN_FRAMESPERSCHED 1
#define PIPE_MAX_FRAMESPERSCHED 1000

#define BUFFER_MIN_SIZE 1
#define BUFFER_MAX_SIZE 1000

#define SSP_MIN_MCLK 1
#define SSP_MAX_MCLK 1000
#define SSP_MIN_BCLK 1
#define SSP_MAX_BCLK 1000
#define SSP_MIN_FSYNC 1
#define SSP_MAX_FSYNC 1000
#define SSP_MIN_VALIDBITS 1
#define SSP_MAX_VALIDBITS 1000
#define SSP_MIN_TDMSLOTWIDTH 1
#define SSP_MAX_TDMSLOTWIDTH 1000
#define SSP_MIN_TDMSLOTS 1
#define SSP_MAX_TDMSLOTS 1000

#define DMIC_MIN_PDMCLK 100000
#define DMIC_MAX_PDMCLK 5000000
#define DMIC_MIN_DUTY 1
#define DMIC_MAX_DUTY 100
#define DMIC_MIN_FIFOSFA 8000
#define DMIC_MAX_FIFOSFA 96000
#define DMIC_MIN_PDMACTIVE 1
#define DMIC_MAX_PDMACTIVE 2
#define DMIC_MIN_FIFOBITSA 16
#define DMIC_MAX_FIFOBITSA 32

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define MAX_ELEM_ARRAY_SIZE 64

struct snd_soc_tplg_hdr *hdr_elem_list[MAX_ELEM_ARRAY_SIZE];
struct snd_soc_tplg_dapm_graph_elem *graph_elem_list[MAX_ELEM_ARRAY_SIZE];
struct snd_soc_tplg_dapm_widget *widget_elem_list[MAX_ELEM_ARRAY_SIZE];
struct snd_soc_tplg_manifest *manifest_elem_list[MAX_ELEM_ARRAY_SIZE];
struct snd_soc_tplg_pcm *pcm_elem_list[MAX_ELEM_ARRAY_SIZE];
struct snd_soc_tplg_link_config *link_config_elem_list[MAX_ELEM_ARRAY_SIZE];
struct snd_soc_tplg_dai *dai_elem_list[MAX_ELEM_ARRAY_SIZE];

#define MAX_PIPELINES 256
int pipeline_comp_count[MAX_PIPELINES];
int pipeline_id[MAX_PIPELINES];
int pipeline_count = 0;

int hdr_elem_count = 0;
int graph_elem_count = 0;
int widget_elem_count = 0;
int manifest_elem_count = 0;
int pcm_elem_count = 0;
int link_config_elem_count = 0;
int dai_elem_count = 0;

struct tplg {
	size_t size;
	unsigned char *data;
	const unsigned char *pos;
};

int print_info = 0;

struct sof_dai_types {
	const char *name;
	enum sof_ipc_dai_type type;
};

static const struct sof_dai_types sof_dais[] = {
	{"SSP", SOF_DAI_INTEL_SSP},
	{"HDA", SOF_DAI_INTEL_HDA},
	{"DMIC", SOF_DAI_INTEL_DMIC},
};

static enum sof_ipc_dai_type find_dai(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_dais); i++) {
		if (strcmp(name, sof_dais[i].name) == 0)
			return sof_dais[i].type;
	}

	return SOF_DAI_INTEL_NONE;
}

struct sof_frame_types {
	const char *name;
	enum sof_ipc_frame frame;
};

static const struct sof_frame_types sof_frames[] = {
	{"s16le", SOF_IPC_FRAME_S16_LE},
	{"s24le", SOF_IPC_FRAME_S24_4LE},
	{"s32le", SOF_IPC_FRAME_S32_LE},
	{"float", SOF_IPC_FRAME_FLOAT},
};

static enum sof_ipc_frame find_format(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_frames); i++) {
		if (strcmp(name, sof_frames[i].name) == 0)
			return sof_frames[i].frame;
	}

	/* use s32le if nothing is specified */
	return SOF_IPC_FRAME_S32_LE;
}

static void sof_dbg_comp_config(struct sof_ipc_comp_config *config)
{
	printf("config: periods snk %d src %d fmt %d\n\n",
	       config->periods_sink, config->periods_source,
	       config->frame_fmt);
}

struct sof_topology_token {
	uint32_t token;
	uint32_t type;
	int (*get_token)(void *elem, void *object, uint32_t offset,
			 uint32_t size);
	uint32_t offset;
	uint32_t size;
};

static int get_token_uint32_t(void *elem, void *object, uint32_t offset,
			      uint32_t size)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	uint32_t *val = object + offset;

	*val = velem->value;
	return 0;
}

static int get_token_uint16_t(void *elem, void *object, uint32_t offset,
			      uint32_t size)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	uint16_t *val = object + offset;

	*val = (uint16_t)velem->value;
	return 0;
}

static int get_token_comp_format(void *elem, void *object, uint32_t offset,
				 uint32_t size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	uint32_t *val = object + offset;

	*val = find_format(velem->string);
	return 0;
}

static int get_token_dai_type(void *elem, void *object, uint32_t offset,
			      uint32_t size)
{
	struct snd_soc_tplg_vendor_string_elem *velem = elem;
	uint32_t *val = object + offset;

	*val = find_dai(velem->string);
	return 0;
}

static const struct sof_topology_token buffer_tokens[] = {
	{SOF_TKN_BUF_SIZE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_buffer, size), 0},
	{SOF_TKN_BUF_CAPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_buffer, caps), 0},
};

static const struct sof_topology_token dai_tokens[] = {
	{SOF_TKN_DAI_DMAC_CONFIG, SND_SOC_TPLG_TUPLE_TYPE_WORD,
	 get_token_uint32_t, offsetof(struct sof_ipc_comp_dai, dmac_config), 0},
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
	 offsetof(struct sof_ipc_comp_dai, type), 0},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_comp_dai, dai_index), 0},
};

static const struct sof_topology_token dai_link_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
	 offsetof(struct sof_ipc_dai_config, type), 0},
	{SOF_TKN_DAI_INDEX, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_config, dai_index), 0},
};

static const struct sof_topology_token sched_tokens[] = {
	{SOF_TKN_SCHED_DEADLINE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
	 get_token_uint32_t, offsetof(struct sof_ipc_pipe_new, deadline), 0},
	{SOF_TKN_SCHED_PRIORITY, SND_SOC_TPLG_TUPLE_TYPE_WORD,
	 get_token_uint32_t, offsetof(struct sof_ipc_pipe_new, priority), 0},
	{SOF_TKN_SCHED_MIPS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_pipe_new, mips), 0},
	{SOF_TKN_SCHED_CORE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_pipe_new, core), 0},
	{SOF_TKN_SCHED_FRAMES, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_pipe_new, frames_per_sched), 0},
	{SOF_TKN_SCHED_TIMER, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_pipe_new, timer), 0},
};

static const struct sof_topology_token volume_tokens[] = {
	{SOF_TKN_VOLUME_RAMP_STEP_TYPE, SND_SOC_TPLG_TUPLE_TYPE_WORD,
	 get_token_uint32_t, offsetof(struct sof_ipc_comp_volume, ramp), 0},
	{SOF_TKN_VOLUME_RAMP_STEP_MS,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_comp_volume, initial_ramp), 0},
};

static const struct sof_topology_token src_tokens[] = {
	{SOF_TKN_SRC_RATE_IN, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_comp_src, source_rate), 0},
	{SOF_TKN_SRC_RATE_OUT, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_comp_src, sink_rate), 0},
};

static const struct sof_topology_token tone_tokens[] = {
};

static const struct sof_topology_token pcm_tokens[] = {
	{SOF_TKN_PCM_DMAC_CONFIG, SND_SOC_TPLG_TUPLE_TYPE_WORD,
	 get_token_uint32_t, offsetof(struct sof_ipc_comp_host, dmac_config),
	 0},
};

static const struct sof_topology_token comp_tokens[] = {
	{SOF_TKN_COMP_PERIOD_SINK_COUNT,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_comp_config, periods_sink), 0},
	{SOF_TKN_COMP_PERIOD_SOURCE_COUNT,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_comp_config, periods_source), 0},
	{SOF_TKN_COMP_FORMAT,
	 SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_comp_format,
	 offsetof(struct sof_ipc_comp_config, frame_fmt), 0},
	{SOF_TKN_COMP_PRELOAD_COUNT,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_comp_config, preload_count), 0},
};

static const struct sof_topology_token ssp_tokens[] = {
	{SOF_TKN_INTEL_SSP_MCLK_KEEP_ACTIVE,
	 SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_ssp_params, mclk_keep_active), 0},
	{SOF_TKN_INTEL_SSP_BCLK_KEEP_ACTIVE,
	 SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_ssp_params, bclk_keep_active), 0},
	{SOF_TKN_INTEL_SSP_FS_KEEP_ACTIVE,
	 SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_ssp_params, fs_keep_active), 0},
	{SOF_TKN_INTEL_SSP_MCLK_ID,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_ssp_params, mclk_id), 0},
	{SOF_TKN_INTEL_SSP_SAMPLE_BITS, SND_SOC_TPLG_TUPLE_TYPE_WORD,
	 get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_ssp_params, sample_valid_bits), 0},
	{SOF_TKN_INTEL_SSP_FRAME_PULSE_WIDTH, SND_SOC_TPLG_TUPLE_TYPE_SHORT,
	 get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_ssp_params, frame_pulse_width), 0},
	{SOF_TKN_INTEL_SSP_QUIRKS, SND_SOC_TPLG_TUPLE_TYPE_WORD,
	 get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_ssp_params, quirks), 0},
};

static const struct sof_topology_token dmic_tokens[] = {
	{SOF_TKN_INTEL_DMIC_DRIVER_VERSION,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_dmic_params, driver_ipc_version),
	 0},
	{SOF_TKN_INTEL_DMIC_CLK_MIN,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_dmic_params, pdmclk_min), 0},
	{SOF_TKN_INTEL_DMIC_CLK_MAX,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_dmic_params, pdmclk_max), 0},
	{SOF_TKN_INTEL_DMIC_SAMPLE_RATE,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_dmic_params, fifo_fs_a), 0},
	{SOF_TKN_INTEL_DMIC_DUTY_MIN,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_params, duty_min), 0},
	{SOF_TKN_INTEL_DMIC_DUTY_MAX,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_params, duty_max), 0},
	{SOF_TKN_INTEL_DMIC_NUM_PDM_ACTIVE,
	 SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_uint32_t,
	 offsetof(struct sof_ipc_dai_dmic_params,
		  num_pdm_active), 0},
	{SOF_TKN_INTEL_DMIC_FIFO_WORD_LENGTH,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_params, fifo_bits_a), 0},
};

static const struct sof_topology_token dmic_pdm_tokens[] = {
	{SOF_TKN_INTEL_DMIC_PDM_CTRL_ID,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, id),
	 0},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_a),
	 0},
	{SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, enable_mic_b),
	 0},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_A,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_a),
	 0},
	{SOF_TKN_INTEL_DMIC_PDM_POLARITY_B,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, polarity_mic_b),
	 0},
	{SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, clk_edge),
	 0},
	{SOF_TKN_INTEL_DMIC_PDM_SKEW,
	 SND_SOC_TPLG_TUPLE_TYPE_SHORT, get_token_uint16_t,
	 offsetof(struct sof_ipc_dai_dmic_pdm_ctrl, skew),
	 0},
};

static const struct sof_topology_token hda_tokens[] = {
};

static void sof_parse_uuid_tokens(void *object,
				  const struct sof_topology_token *tokens,
				  int count,
				  struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_uuid_elem *elem;
	int i, j;

	/* parse element by element */
	for (i = 0; i < array->num_elems; i++) {
		elem = &array->uuid[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_UUID)
				continue;

			/* match token id */
			if (tokens[j].token != elem->token)
				continue;

			/* matched - now load token */
			tokens[j].get_token(elem, object, tokens[j].offset,
					    tokens[j].size);
		}
	}
}

static void sof_parse_string_tokens(void *object,
				    const struct sof_topology_token *tokens,
				    int count,
				    struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_string_elem *elem;
	int i, j;

	/* parse element by element */
	for (i = 0; i < array->num_elems; i++) {
		elem = &array->string[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_STRING)
				continue;

			/* match token id */
			if (tokens[j].token != elem->token)
				continue;

			/* matched - now load token */
			tokens[j].get_token(elem, object, tokens[j].offset,
					    tokens[j].size);
		}
	}
}

static void sof_parse_word_tokens(uint32_t *ind, void *object,
				  const struct sof_topology_token *tokens,
				  int count,
				  struct snd_soc_tplg_vendor_array *array)
{
	/* struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp); */
	struct snd_soc_tplg_vendor_value_elem *elem;
	size_t size = sizeof(struct sof_ipc_dai_dmic_pdm_ctrl);
	int i, j;
	uint32_t offset;
	uint32_t *index = ind;

	/* parse element by element */
	for (i = 0; i < array->num_elems; i++) {
		elem = &array->value[i];

		/* search for token */
		for (j = 0; j < count; j++) {
			/* match token type */
			if (!(tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_WORD ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_SHORT))
				continue;

			/* match token id */
			if (tokens[j].token != elem->token)
				continue;

			/* pdm config array index */
			/* if (sdev->private) */
			/* index = (uint32_t *)sdev->private; */

			/* matched - determine offset */
			switch (tokens[j].token) {
			case SOF_TKN_INTEL_DMIC_PDM_CTRL_ID:
				/* inc number of pdm array index */
				if (index)
					(*index)++;
				/* fallthrough */
			case SOF_TKN_INTEL_DMIC_PDM_MIC_A_Enable:
			case SOF_TKN_INTEL_DMIC_PDM_MIC_B_Enable:
			case SOF_TKN_INTEL_DMIC_PDM_POLARITY_A:
			case SOF_TKN_INTEL_DMIC_PDM_POLARITY_B:
			case SOF_TKN_INTEL_DMIC_PDM_CLK_EDGE:
			case SOF_TKN_INTEL_DMIC_PDM_SKEW:

				/* check if array index is valid */
				if (!index || *index == 0) {
					printf("error: invalid array offset\n");
					continue;
				} else {
					/* offset within the pdm config array */
					offset = size * (*index - 1);
				}
				break;
			default:
				offset = 0;
				break;
			}

			/* load token */
			tokens[j].get_token(elem, object,
					    offset + tokens[j].offset,
					    tokens[j].size);
		}
	}
}

static int sof_parse_tokens(void *object,
			    const struct sof_topology_token *tokens,
			    int count,
			    struct snd_soc_tplg_vendor_array *array,
			    int priv_size)
{
	int asize;
	uint32_t index = 0;

	while (priv_size > 0) {
		asize = array->size;

		/* validate asize */
		if (asize < 0) { /* FIXME: A zero-size array makes no sense */
			printf("error: invalid array size 0x%x\n", asize);
			return -1;
		}

		/* make sure there is enough data before parsing */
		priv_size -= asize;
		if (priv_size < 0) {
			printf("error: invalid array size 0x%x\n", asize);
			return -1;
		}

		/* call correct parser depending on type */
		switch (array->type) {
		case SND_SOC_TPLG_TUPLE_TYPE_UUID:
			sof_parse_uuid_tokens(object, tokens, count, array);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_STRING:
			sof_parse_string_tokens(object, tokens, count, array);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_BOOL:
		case SND_SOC_TPLG_TUPLE_TYPE_BYTE:
		case SND_SOC_TPLG_TUPLE_TYPE_WORD:
		case SND_SOC_TPLG_TUPLE_TYPE_SHORT:
			sof_parse_word_tokens(&index, object, tokens, count,
					      array);
			break;
		default:
			printf("error: unknown token type %d\n", array->type);
			return -1;
		}

		/* next array */
		array = (void *)array + asize;
	}

	return 0;
}

static int sof_widget_load_buffer(struct sof_ipc_buffer *buffer,
				  int pipeline_id, int component_id,
				  struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_buffer);
	int ret;

	memset(buffer, 0, size);
	buffer->comp.hdr.size = size;
	buffer->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_BUFFER_NEW;
	buffer->comp.id = component_id;
	buffer->comp.type = SOF_COMP_BUFFER;
	buffer->comp.pipeline_id = pipeline_id;

	ret = sof_parse_tokens(buffer, buffer_tokens,
			       ARRAY_SIZE(buffer_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf("error: parse buffer tokens failed %d\n",
		       private->size);
		return ret;
	}

	return 0;
}

static int sof_widget_load_dai(struct sof_ipc_comp_dai *comp_dai,
			       int pipeline_id, int component_id,
			       struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_comp_dai);
	int ret;

	memset(comp_dai, 0, size);
	comp_dai->comp.hdr.size = size;
	comp_dai->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	comp_dai->comp.id = component_id;
	comp_dai->comp.type = SOF_COMP_DAI;
	comp_dai->comp.pipeline_id = pipeline_id;

	ret = sof_parse_tokens(comp_dai, dai_tokens,
			       ARRAY_SIZE(dai_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf("error: parse dai tokens failed %d\n",
		       private->size);
		return ret;
	}

	ret = sof_parse_tokens(&comp_dai->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf("error: parse dai.cfg tokens failed %d\n",
		       private->size);
		return ret;
	}

	return ret;
}

static int sof_widget_load_pga(struct sof_ipc_comp_volume *volume,
			       int pipeline_id, int component_id,
			       struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_comp_volume);
	int ret;

	if (tw->num_kcontrols != 1) {
		printf( "error: invalid kcontrol count %d for volume\n",
			tw->num_kcontrols);
		return -1;
	}

	/* configure volume IPC message */
	memset(volume, 0, size);
	volume->comp.hdr.size = size;
	volume->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	volume->comp.id = component_id;
	volume->comp.type = SOF_COMP_VOLUME;
	volume->comp.pipeline_id = pipeline_id;

	ret = sof_parse_tokens(volume, volume_tokens,
			       ARRAY_SIZE(volume_tokens), private->array,
			       private->size);

	if (ret != 0) {
		printf( "error: parse volume tokens failed %d\n",
			private->size);
		return ret;
	}

	ret = sof_parse_tokens(&volume->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       private->size);

	if (ret != 0) {
		printf("error: parse volume.cfg tokens failed %d\n",
		       private->size);
		return ret;
	}

	return ret;
}

static int sof_widget_load_pcm(struct sof_ipc_comp_host *host,
			       int pipeline_id, int component_id,
			       enum sof_ipc_stream_direction dir,
			       struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_comp_host);
	int ret;

	/* configure mixer IPC message */
	memset(host, 0, size);
	host->comp.hdr.size = size;
	host->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	host->comp.id = component_id;
	host->comp.type = SOF_COMP_HOST;
	host->comp.pipeline_id = pipeline_id;
	host->direction = dir;

	ret = sof_parse_tokens(host, pcm_tokens,
			       ARRAY_SIZE(pcm_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse host tokens failed %d\n",
			private->size);
		return ret;
	}

	return ret;
}

static int sof_widget_load_mixer(struct sof_ipc_comp_mixer *mixer,
				 int pipeline_id, int component_id,
				 struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_comp_mixer);
	int ret;

	/* configure mixer IPC message */
	memset(mixer, 0, size);
	mixer->comp.hdr.size = size;
	mixer->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	mixer->comp.id = component_id;
	mixer->comp.type = SOF_COMP_MIXER;
	mixer->comp.pipeline_id = pipeline_id;

	ret = sof_parse_tokens(&mixer->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       private->size);

	if (ret != 0) {
		printf( "error: parse mixer.cfg tokens failed %d\n",
			private->size);
		return ret;
	}

	return ret;
}

static int sof_widget_load_pipeline(struct sof_ipc_pipe_new *pipeline,
				    int pipeline_id, int component_id,
				    struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_pipe_new);
	int ret;

	/* configure dai IPC message */
	memset(pipeline, 0, size);
	pipeline->hdr.size = size;
	pipeline->hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_PIPE_NEW;
	pipeline->pipeline_id = pipeline_id;
	pipeline->comp_id = component_id;

	ret = sof_parse_tokens(pipeline, sched_tokens,
			       ARRAY_SIZE(sched_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse pipeline tokens failed %d\n",
			private->size);
		return ret;
	}

	return ret;
}

static int sof_widget_load_src(struct sof_ipc_comp_src *src,
			       int pipeline_id, int component_id,
			       struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_comp_src);
	int ret;

	/* configure src IPC message */
	memset(&src, 0, size);
	src->comp.hdr.size = size;
	src->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	src->comp.id = component_id;
	src->comp.type = SOF_COMP_SRC;
	src->comp.pipeline_id = pipeline_id;

	ret = sof_parse_tokens(src, src_tokens,
			       ARRAY_SIZE(src_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse src tokens failed %d\n",
			private->size);
		return ret;
	}

	ret = sof_parse_tokens(&src->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse src.cfg tokens failed %d\n",
			private->size);
		return ret;
	}

	return ret;
}

static int sof_widget_load_siggen(struct sof_ipc_comp_tone *tone,
				  int pipeline_id, int component_id,
				  struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_soc_tplg_private *private = &tw->priv;
	size_t size = sizeof(struct sof_ipc_comp_tone);
	int ret;

	/* configure mixer IPC message */
	memset(&tone, 0, size);
	tone->comp.hdr.size = size;
	tone->comp.hdr.cmd = SOF_IPC_GLB_TPLG_MSG | SOF_IPC_TPLG_COMP_NEW;
	tone->comp.id = component_id;
	tone->comp.type = SOF_COMP_TONE;
	tone->comp.pipeline_id = pipeline_id;

	ret = sof_parse_tokens(tone, tone_tokens,
			       ARRAY_SIZE(tone_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse tone tokens failed %d\n",
			private->size);
		return ret;
	}

	ret = sof_parse_tokens(&tone->config, comp_tokens,
			       ARRAY_SIZE(comp_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse tone.cfg tokens failed %d\n",
			private->size);
		return ret;
	}

	return ret;
}

static void sof_dai_set_format(struct snd_soc_tplg_hw_config *hw_config,
			       struct sof_ipc_dai_config *config)
{
	/* clock directions wrt codec */
	if (hw_config->bclk_master == SND_SOC_TPLG_BCLK_CM) {
		/* codec is bclk master */
		if (hw_config->fsync_master == SND_SOC_TPLG_FSYNC_CM)
			config->format |= SOF_DAI_FMT_CBM_CFM;
		else
			config->format |= SOF_DAI_FMT_CBM_CFS;
	} else {
		/* codec is bclk slave */
		if (hw_config->fsync_master == SND_SOC_TPLG_FSYNC_CM)
			config->format |= SOF_DAI_FMT_CBS_CFM;
		else
			config->format |= SOF_DAI_FMT_CBS_CFS;
	}

	/* inverted clocks ? */
	if (hw_config->invert_bclk) {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_IB_IF;
		else
			config->format |= SOF_DAI_FMT_IB_NF;
	} else {
		if (hw_config->invert_fsync)
			config->format |= SOF_DAI_FMT_NB_IF;
		else
			config->format |= SOF_DAI_FMT_NB_NF;
	}
}

static int sof_link_ssp_load(struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config)
{
	struct snd_soc_tplg_private *private = &cfg->priv;
	uint32_t size = sizeof(*config);
	int ret;

	/* handle master/slave and inverted clocks */
	sof_dai_set_format(hw_config, config);

	/* init IPC */
	memset(&config->ssp, 0, sizeof(struct sof_ipc_dai_ssp_params));
	config->hdr.size = size;

	ret = sof_parse_tokens(&config->ssp, ssp_tokens,
			       ARRAY_SIZE(ssp_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse ssp tokens failed %d\n",
			private->size);
		return ret;
	}

	config->ssp.mclk_rate = hw_config->mclk_rate;
	config->ssp.bclk_rate = hw_config->bclk_rate;
	config->ssp.fsync_rate = hw_config->fsync_rate;
	config->ssp.tdm_slots = hw_config->tdm_slots;
	config->ssp.tdm_slot_width = hw_config->tdm_slot_width;
	config->ssp.mclk_direction = hw_config->mclk_direction;
	config->ssp.rx_slots = hw_config->rx_slots;
	config->ssp.tx_slots = hw_config->tx_slots;

	if (print_info) {
		printf("loading link config SSP\n");
		printf("dai_index %d\n", config->dai_index);
		printf("format 0x%x \n", config->format);
		printf("mclk %d \n",  config->ssp.mclk_rate);
		printf("bclk %d \n", config->ssp.bclk_rate);
		printf("fclk %d \n",  config->ssp.fsync_rate);
		printf("width (%d)%d \n", config->ssp.sample_valid_bits,
		       config->ssp.tdm_slot_width);
		printf("slots %d \n", config->ssp.tdm_slots);
		printf("mclk id %d\n", config->ssp.mclk_id);
		printf("\n");
	}

	return ret;
}

static int sof_link_dmic_load(struct snd_soc_tplg_link_config *cfg,
			      struct snd_soc_tplg_hw_config *hw_config,
			      struct sof_ipc_dai_config *config)
{
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct sof_ipc_dai_config *ipc_config;
	uint32_t size;
	int ret, j;

	memset(&config->dmic, 0, sizeof(struct sof_ipc_dai_dmic_params));

	/* get DMIC tokens */
	ret = sof_parse_tokens(&config->dmic, dmic_tokens,
			       ARRAY_SIZE(dmic_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse dmic tokens failed %d\n",
			private->size);
		return ret;
	}

	/*
	 * allocate memory for common dai params, dmic params
	 * and dmic pdm controller params
	 */
	ipc_config = malloc(sizeof(*config) +
			    sizeof(struct sof_ipc_dai_dmic_pdm_ctrl) *
			    config->dmic.num_pdm_active);
	if (!ipc_config) {
		printf( "error: allocating memory for config\n");
		return -1;
	}

	/* copy the common dai config and dmic params */
	memcpy(ipc_config, config, sizeof(*config));

	/* get DMIC PDM tokens */
	ret = sof_parse_tokens(&ipc_config->dmic.pdm[0], dmic_pdm_tokens,
			       ARRAY_SIZE(dmic_pdm_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse dmic pdm tokens failed %d\n",
			private->size);
		free(ipc_config);
		return ret;
	}

	/* set IPC header size */
	size = sizeof(*ipc_config);
	ipc_config->hdr.size = size;

	if (print_info) {
		printf("loadin link config DMIC\n");
		printf("dai_index %d\n", ipc_config->dai_index);
		printf("driver version %d\n",
		       ipc_config->dmic.driver_ipc_version);
		printf("pdmclk_min %d\n", ipc_config->dmic.pdmclk_min);
		printf("pdm_clkmax %d\n", ipc_config->dmic.pdmclk_max);
		printf("duty_min %hd\n", ipc_config->dmic.duty_min);
		printf("duty_max %hd\n", ipc_config->dmic.duty_max);
		printf("fifo_fs %d\n", ipc_config->dmic.fifo_fs_a);
		printf("num_pdms active %d\n", ipc_config->dmic.num_pdm_active);
		printf("fifo word length %hd\n", ipc_config->dmic.fifo_bits_a);

		for (j = 0; j < ipc_config->dmic.num_pdm_active; j++) {
			printf("pdm %hd mic a %hd mic b %hd\n",
			       ipc_config->dmic.pdm[j].id,
			       ipc_config->dmic.pdm[j].enable_mic_a,
			       ipc_config->dmic.pdm[j].enable_mic_b);
			printf("pdm %hd polarity a %hd polarity b %hd\n",
			       ipc_config->dmic.pdm[j].id,
			       ipc_config->dmic.pdm[j].polarity_mic_a,
			       ipc_config->dmic.pdm[j].polarity_mic_b);
			printf("pdm %hd clk_edge %hd skew %hd\n",
			       ipc_config->dmic.pdm[j].id,
			       ipc_config->dmic.pdm[j].clk_edge,
			       ipc_config->dmic.pdm[j].skew);
		}
		printf("\n");
	}

	ipc_config->dmic.fifo_bits_b = ipc_config->dmic.fifo_bits_a;

	free(ipc_config);

	return ret;
}

static int sof_link_hda_load(struct snd_soc_tplg_link_config *cfg,
			     struct snd_soc_tplg_hw_config *hw_config,
			     struct sof_ipc_dai_config *config)
{
	struct snd_soc_tplg_private *private = &cfg->priv;
	uint32_t size = sizeof(*config);
	int ret;

	/* init IPC */
	memset(&config->hda, 0, sizeof(struct sof_ipc_dai_hda_params));
	config->hdr.size = size;

	/* get any bespoke DAI tokens */
	ret = sof_parse_tokens(config, hda_tokens,
			       ARRAY_SIZE(hda_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse hda tokens failed %d\n",
			private->size);
		return ret;
	}

	if (print_info) {
		printf("tplg: config HDA%d fmt 0x%x\n",
		       config->dai_index, config->format);
	}

	return ret;
}

/* DAI link - used for any driver specific init */
static int sof_link_load(struct sof_ipc_dai_config *config,
			 struct snd_soc_tplg_link_config *cfg)
{
	struct snd_soc_tplg_private *private = &cfg->priv;
	size_t size = sizeof(struct sof_ipc_dai_config);
	struct snd_soc_tplg_hw_config *hw_config;
	int ret = 0, i = 0;

	/* only support 1 config atm */
	if (cfg->num_hw_configs != 1) {
		printf( "error: unexpected DAI config count %d\n",
			cfg->num_hw_configs);
		return -1;
	}

	/* check we have some tokens - we need at least DAI type */
	if (private->size == 0) {
		printf( "error: expected tokens for DAI, none found\n");
		return -1;
	}

	memset(config, 0, size);

	/* get any common DAI tokens */
	ret = sof_parse_tokens(config, dai_link_tokens,
			       ARRAY_SIZE(dai_link_tokens), private->array,
			       private->size);
	if (ret != 0) {
		printf( "error: parse link tokens failed %d\n",
			private->size);
		return ret;
	}

	/* configure dai IPC message */
	hw_config = &cfg->hw_config[0];

	config->hdr.cmd = SOF_IPC_GLB_DAI_MSG | SOF_IPC_DAI_CONFIG;
	config->format = hw_config->fmt;

	/* now load DAI specific data and send IPC - type comes from token */
	switch (config->type) {
	case SOF_DAI_INTEL_SSP:
		ret = sof_link_ssp_load(cfg, hw_config, config);
		break;
	case SOF_DAI_INTEL_DMIC:
		ret = sof_link_dmic_load(cfg, hw_config, config);
		break;
	case SOF_DAI_INTEL_HDA:
		ret = sof_link_hda_load(cfg, hw_config, config);
		break;
	default:
		printf( "error: invalid DAI type %d\n", config->type);
		ret = -1;
		break;
	}
	if (ret < 0)
		return ret;

	for (i = 0; i < cfg->num_streams; i++)
		printf("link config stream %d name %s", i, cfg->stream_name);

	return 0;
}

static int sof_manifest_load(struct snd_soc_tplg_manifest *manifest)
{
	if(!manifest)
		return -1;

	if (print_info) {
		printf("loading manifest element\n");
		printf("control_elems: %d\n", manifest->control_elems);
		printf("widget_elems: %d\n", manifest->widget_elems);
		printf("graph_elems: %d\n", manifest->graph_elems);
		printf("pcm_elems: %d\n", manifest->pcm_elems);
		printf("dai_link_elems: %d\n", manifest->dai_link_elems);
		printf("dai_elems: %d\n\n", manifest->dai_elems);
	}

	return 0;
}

static int sof_pcm_load(struct snd_soc_tplg_pcm *pcm)
{
	int i = 0;

	if(!pcm)
		return -1;

	if (print_info) {
		printf("loading pcm element\n");
		printf("pcm_name: %s\n", pcm->pcm_name);
		printf("dai_name: %s\n", pcm->dai_name);
		printf("pcm_id: %d\n", pcm->pcm_id);
		printf("dai_id: %d\n", pcm->dai_id);
		printf("playback: %d\n", pcm->playback);
		printf("capture: %d\n", pcm->capture);
		printf("compress: %d\n", pcm->compress);
		printf("num_streams: %d\n", pcm->num_streams);
		printf("flag_mask: %d\n", pcm->flag_mask);
		printf("flags: %d\n\n",pcm->flags);

		/* struct snd_soc_tplg_stream_caps caps[2]; */
		for (i = 0; i < pcm->num_streams; i++) {
			printf("stream %d name %s\n", i, pcm->stream->name);
		}
	}

	return 0;
}

static int sof_graph_load(struct snd_soc_tplg_dapm_graph_elem *graph)
{
	if (!graph)
		return -1;

	if (print_info) {
		printf("loading graph elem\n");
		printf("graph sink:%s source:%s\n",
		       graph->sink, graph->source);
	}

	return 0;
}

static int sof_mixer_load(struct snd_soc_tplg_mixer_control *mc)
{
	int i = 0;

	if (!mc)
		return -1;

	if (print_info) {
		printf("loading mixer elem\n");
		printf("mixer name %s\n",mc->hdr.name);
		printf("access %d\n", mc->hdr.access);
		printf("num_channel %d\n", mc->num_channels);
		printf("max %d\n", mc->max);
		printf("min %d\n", mc->min);
		printf("invert %d\n", mc->invert);
		for (i = 0; i < mc->num_channels; i++) {
			printf("chan %d reg %d\n", i, mc->channel[i].reg);
			printf("chan %d shift %d\n", i, mc->channel[i].shift);
			printf("chan %d id %d\n", i, mc->channel[i].id);
		}
		printf("\n");
	}

	return 0;
}

int create_hdr(struct snd_soc_tplg_hdr **hdr,
	       struct tplg *tplg)
{
	size_t size = sizeof(struct snd_soc_tplg_hdr);
	*hdr = (struct snd_soc_tplg_hdr *)malloc(size);

	if (!*hdr) {
		printf("error: mem alloc\n");
		return -1;
	}

	memcpy(*hdr, tplg->pos, size);

	tplg->pos += size;

	return 0;
}

int create_graph_elem(struct snd_soc_tplg_dapm_graph_elem
		      **graph_elem, struct tplg *tplg)
{
	size_t size = sizeof(struct snd_soc_tplg_dapm_graph_elem);
	*graph_elem = (struct snd_soc_tplg_dapm_graph_elem *)malloc(size);

	if (!*graph_elem) {
		printf("error: mem alloc\n");
		return -1;
	}

	memcpy(*graph_elem, tplg->pos, size);

	tplg->pos += size;

	return 0;
}

int create_manifest(struct snd_soc_tplg_manifest **manifest_elem,
		    struct tplg *tplg)
{
	struct snd_soc_tplg_manifest *p =
		(struct snd_soc_tplg_manifest *)tplg->pos;
	size_t size = sizeof(struct snd_soc_tplg_manifest) + p->priv.size;
	*manifest_elem = (struct snd_soc_tplg_manifest *)malloc(size);

	if (!*manifest_elem) {
		printf("error: mem alloc\n");
		return -1;
	}

	memcpy(*manifest_elem, tplg->pos, size);

	tplg->pos += size;

	return 0;
}

int create_pcm(struct snd_soc_tplg_pcm **pcm_elem,
	       struct tplg *tplg)
{
	struct snd_soc_tplg_pcm *pcm_p =
		(struct snd_soc_tplg_pcm *)tplg->pos;
	size_t size = sizeof(struct snd_soc_tplg_pcm) + pcm_p->priv.size;
	*pcm_elem = (struct snd_soc_tplg_pcm *)malloc(size);

	if (!*pcm_elem) {
		printf("error: mem alloc\n");
		return -1;
	}

	memcpy(*pcm_elem, tplg->pos, size);

	tplg->pos += size;

	return 0;
}

int create_link_config(struct snd_soc_tplg_link_config
		       **link_config_elem,
		       struct tplg *tplg, int *index)
{
	struct snd_soc_tplg_link_config* p =
		(struct snd_soc_tplg_link_config *)tplg->pos;
	size_t size = sizeof(struct snd_soc_tplg_link_config) + p->priv.size;
	*link_config_elem = (struct snd_soc_tplg_link_config *)malloc(size);

	if (!*link_config_elem) {
		printf("error: mem alloc\n");
		return -1;
	}

	memcpy(*link_config_elem, tplg->pos, size);

	/* sof_link_load(*index, *link_config_elem); */

	tplg->pos += size;

	return 0;
}

int create_dai(struct snd_soc_tplg_dai **dai_elem,
	       struct tplg *tplg)
{
	struct snd_soc_tplg_dai* p = (struct snd_soc_tplg_dai *)tplg->pos;
	size_t size = sizeof(struct snd_soc_tplg_dai) + p->priv.size;
	*dai_elem = (struct snd_soc_tplg_dai *)malloc(size);

	if (!*dai_elem) {
		printf("error: mem alloc\n");
		return -1;
	}

	memcpy(*dai_elem, tplg->pos, size);

	tplg->pos += size;

	return 0;
}

void print_dapm_data(struct snd_soc_tplg_dapm_widget *widget_elem)
{

}

void print_widget_data(struct snd_soc_tplg_dapm_widget *widget_elem)
{
	struct sof_ipc_comp_mixer mixer;
	struct sof_ipc_comp_src src;
	struct sof_ipc_comp_volume volume;
	struct sof_ipc_buffer buffer;
	struct sof_ipc_comp_dai comp_dai;
	struct sof_ipc_comp_host host;
	struct sof_ipc_pipe_new pipeline;
	struct sof_ipc_comp_tone tone;

	printf("widget name: %s\n", (widget_elem)->name);
	printf("widget stream: %s\n", (widget_elem)->sname);
	printf("widget size %d and priv size %d\n", (widget_elem)->size,
	       (widget_elem)->priv.size);
	printf("widget controls: %d\n", (widget_elem)->num_kcontrols);
	printf("widget id: %d\n", (widget_elem)->id);

	switch ((widget_elem)->id)
	{
	case SND_SOC_TPLG_DAPM_INPUT:
		printf("input data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_OUTPUT:
		printf("output data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_MUX:
		printf("mux data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_MIXER:
		sof_widget_load_mixer(&mixer, 0, 0, widget_elem);
		printf("comp id %d\n", mixer.comp.id);
		printf("comp type %d\n", mixer.comp.type);
		printf("comp pipeline_id %d\n", mixer.comp.pipeline_id);
		sof_dbg_comp_config(&mixer.config);
		break;
	case SND_SOC_TPLG_DAPM_PGA:
		sof_widget_load_pga(&volume, 0, 0, widget_elem);
		printf("channels %d\n", volume.channels);
		printf("min_value: %d\n", volume.min_value);
		printf("max_value: %d\n", volume.max_value);
		printf("ramp: %d\n", volume.ramp);
		printf("initial ramp: %d\n", volume.initial_ramp);
		printf("comp id: %d\n", volume.comp.id);
		printf("comp type: %d\n", volume.comp.type);
		printf("comp pipeline_id: %d\n", volume.comp.pipeline_id);
		sof_dbg_comp_config(&volume.config);
		break;
	case SND_SOC_TPLG_DAPM_OUT_DRV:
		printf("drv data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_ADC:
		printf("adc data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_DAC:
		printf("dac data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_SWITCH:
		printf("switch data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_PRE:
		printf("pre data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_POST:
		printf("post data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_AIF_IN:
		sof_widget_load_pcm(&host, 0, 0, SOF_IPC_STREAM_CAPTURE,
				    widget_elem);
		printf("direction %d\n", host.direction);
		printf("comp id %d\n", host.comp.id);
		printf("comp type %d\n", host.comp.type);
		printf("comp pipeline_id %d\n", host.comp.pipeline_id);
		sof_dbg_comp_config(&host.config);
		break;
	case SND_SOC_TPLG_DAPM_AIF_OUT:
		sof_widget_load_pcm(&host, 0, 0, SOF_IPC_STREAM_PLAYBACK,
				    widget_elem);
		printf("direction %d\n", host.direction);
		printf("comp id %d\n", host.comp.id);
		printf("comp type %d\n", host.comp.type);
		printf("comp pipeline_id %d\n", host.comp.pipeline_id);
		sof_dbg_comp_config(&host.config);
		break;
	case SND_SOC_TPLG_DAPM_DAI_IN:
	case SND_SOC_TPLG_DAPM_DAI_OUT:
		sof_widget_load_dai(&comp_dai, 0, 0, widget_elem);
		printf("comp id: %d\n", comp_dai.comp.id);
		printf("comp type: %d\n", comp_dai.comp.type);
		printf("comp pipeline_id: %d\n",  comp_dai.comp.pipeline_id);
		printf("comp dai_index: %d\n",  comp_dai.dai_index);
		sof_dbg_comp_config(&comp_dai.config);
		break;
	case SND_SOC_TPLG_DAPM_DAI_LINK:
		printf("dai link data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_BUFFER:
		sof_widget_load_buffer(&buffer, 0, 0, widget_elem);
		printf("size: %d\n",  buffer.size);
		printf("caps: 0x%x\n",	buffer.caps);
		printf("comp id: %d\n", buffer.comp.id);
		printf("comp type: %d\n\n", buffer.comp.type);
		break;
	case SND_SOC_TPLG_DAPM_SCHEDULER:
		sof_widget_load_pipeline(&pipeline, 0, 0, widget_elem);
		printf("deadline %d\n", pipeline.deadline);
		printf("pri %d\n", pipeline.priority);
		printf("mips %d\n", pipeline.mips);
		printf("core %d\n", pipeline.core);
		printf("frames %d\n", pipeline.frames_per_sched);
		printf("pipeline id %d\n", pipeline.pipeline_id);
		printf("comp %d\n", pipeline.comp_id);
		printf("scheduling comp id %d\n\n",  pipeline.sched_id);
		break;
	case SND_SOC_TPLG_DAPM_EFFECT:
		printf("effect data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_SIGGEN:
		sof_widget_load_siggen(&tone, 0, 0, widget_elem);
		printf("comp id %d\n", tone.comp.id);
		printf("scheduling comp id %d\n", tone.comp.pipeline_id);
		printf("frequency %d\n", tone.frequency);
		printf("amplitude %d\n", tone.amplitude);
		sof_dbg_comp_config(&tone.config);
		break;
	case SND_SOC_TPLG_DAPM_SRC:
		sof_widget_load_src(&src, 0, 0, widget_elem);
		printf("comp id %d\n", src.comp.id);
		printf("scheduling comp id %d\n", src.comp.pipeline_id);
		printf("source rate %d\n", src.source_rate);
		printf("sink rate %d\n", src.sink_rate);
		sof_dbg_comp_config(&src.config);
		break;
	case SND_SOC_TPLG_DAPM_ASRC:
		printf("asrc data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_ENCODER:
		printf("encoder data printing not supported\n\n");
		break;
	case SND_SOC_TPLG_DAPM_DECODER:
		printf("decoder data printing not supported\n\n");
		break;
	default:
		printf("unknown widget id\n");
	}
}

void print_widget_type(struct snd_soc_tplg_dapm_widget *widget_elem)
{
	switch ((widget_elem)->id)
	{
	case SND_SOC_TPLG_DAPM_INPUT:
		printf("found widget input\n");
		break;
	case SND_SOC_TPLG_DAPM_OUTPUT:
		printf("found widget output\n");
		break;
	case SND_SOC_TPLG_DAPM_MUX:
		printf("found widget mux\n");
		break;
	case SND_SOC_TPLG_DAPM_MIXER:
		printf("found widget mixer\n");
		break;
	case SND_SOC_TPLG_DAPM_PGA:
		printf("found widget pga\n");
		break;
	case SND_SOC_TPLG_DAPM_OUT_DRV:
		printf("found widget out drv\n");
		break;
	case SND_SOC_TPLG_DAPM_ADC:
		printf("found widget adc\n");
		break;
	case SND_SOC_TPLG_DAPM_DAC:
		printf("found widget dac\n");
		break;
	case SND_SOC_TPLG_DAPM_SWITCH:
		printf("found widget switch\n");
		break;
	case SND_SOC_TPLG_DAPM_PRE:
		printf("found widget pre\n");
		break;
	case SND_SOC_TPLG_DAPM_POST:
		printf("found widget post\n");
		break;
	case SND_SOC_TPLG_DAPM_AIF_IN:
		printf("found widget aif in\n");
		break;
	case SND_SOC_TPLG_DAPM_AIF_OUT:
		printf("found widget aif out \n");
		break;
	case SND_SOC_TPLG_DAPM_DAI_IN:
	case SND_SOC_TPLG_DAPM_DAI_OUT:
		printf("found widget dai\n");
		break;
	case SND_SOC_TPLG_DAPM_DAI_LINK:
		printf("found widget dai link\n");
		break;
	case SND_SOC_TPLG_DAPM_BUFFER:
		printf("found widget buffer\n");
		break;
	case SND_SOC_TPLG_DAPM_SCHEDULER:
		printf("found widget scheduler\n");
		break;
	case SND_SOC_TPLG_DAPM_EFFECT:
		printf("found widget effect\n");
		break;
	case SND_SOC_TPLG_DAPM_SIGGEN:
		printf("found widget siggen\n");
		break;
	case SND_SOC_TPLG_DAPM_SRC:
		printf("found widget src\n");
		break;
	case SND_SOC_TPLG_DAPM_ASRC:
		printf("found widget asrc\n");
		break;
	case SND_SOC_TPLG_DAPM_ENCODER:
		printf("found widget encoder\n");
		break;
	case SND_SOC_TPLG_DAPM_DECODER:
		printf("found widget decoder\n");
		break;
	default:
		printf("unknown widget id\n");
	}

	if (print_info)
		print_widget_data(widget_elem);
}

int create_widget(struct snd_soc_tplg_dapm_widget **widget_elem,
		  struct tplg *tplg, int component_id,
		  int pipeline_id)
{
	struct snd_soc_tplg_dapm_widget *widget_p =
		(struct snd_soc_tplg_dapm_widget*)tplg->pos;
	size_t size = sizeof(struct snd_soc_tplg_dapm_widget) +
		widget_p->priv.size;
	*widget_elem = (struct snd_soc_tplg_dapm_widget *)malloc(size);

	if (!*widget_elem) {
		printf("error: mem alloc\n");
		return -1;
	}

	memcpy(*widget_elem, tplg->pos, size);

	tplg->pos += size;

	/* print info */
	print_widget_type(*widget_elem);

	struct snd_soc_tplg_mixer_control *mc;

	if ((*widget_elem)->num_kcontrols > 0) {
		struct snd_soc_tplg_ctl_hdr *ctl_hdr =
			(struct snd_soc_tplg_ctl_hdr *)tplg->pos;

		switch (ctl_hdr->ops.info) {
		case SND_SOC_TPLG_CTL_VOLSW:
		case SND_SOC_TPLG_CTL_STROBE:
		case SND_SOC_TPLG_CTL_VOLSW_SX:
		case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		case SND_SOC_TPLG_CTL_RANGE:
		case SND_SOC_TPLG_DAPM_CTL_VOLSW:
		case SND_SOC_TPLG_DAPM_CTL_PIN:
			mc = (struct snd_soc_tplg_mixer_control *)tplg->pos;
			tplg->pos += sizeof(struct snd_soc_tplg_mixer_control) +
				mc->priv.size;
			/* sof_mixer_load(mc); */
			break;
		case SND_SOC_TPLG_CTL_ENUM:
		case SND_SOC_TPLG_CTL_BYTES:
		case SND_SOC_TPLG_CTL_ENUM_VALUE:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT:
		case SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE:
		default:
			printf("control type not supported\n");
			break;
		}
	}

	return 0;
}

int parse_dapm(struct tplg *tplg)
{
	struct snd_soc_tplg_hdr *hdr;
	struct snd_soc_tplg_dapm_graph_elem *graph_elem;
	struct snd_soc_tplg_dapm_widget *w_elem;
	struct snd_soc_tplg_manifest *manifest_elem;
	struct snd_soc_tplg_pcm *pcm_elem;
	struct snd_soc_tplg_link_config *link_config_elem;
	struct snd_soc_tplg_dai *dai_elem;

	int ret = 0;
	int i = 0;
	int comp_count = 0;
	int sched_found = 0;

	while (1) {
		ret = create_hdr(&hdr, tplg);
		if (ret != 0) {
			printf("error: reading dapm header\n");
			return -1;
		}

		hdr_elem_list[hdr_elem_count] = hdr;
		hdr_elem_count++;

		switch (hdr->type) {
		case SND_SOC_TPLG_TYPE_DAPM_GRAPH:
			if (print_info) {
				printf("graph hdr index %d hdr count %d\n",
				       hdr->index, hdr->count);
			}
			for (i = 0; i < hdr->count; i++) {
				printf("found graph elem\n");
				ret = create_graph_elem(&graph_elem, tplg);
				if (ret != 0) {
					printf("error: reading graph elem\n");
					return -1;
				}
				graph_elem_list[graph_elem_count] = graph_elem;
				graph_elem_count++;
				/* print info */
				sof_graph_load(graph_elem);
			}
			break;
		case SND_SOC_TPLG_TYPE_DAPM_WIDGET:
				printf("widget hdr index %d hdr count %d\n",
				       hdr->index, hdr->count);
			for (i = 0; i < hdr->count; i++) {
				ret = create_widget(&w_elem,
						    tplg,
						    comp_count++,
						    hdr->index);
				if (ret != 0) {
					printf("error: reading widget elem\n");
					return -1;
				}
				widget_elem_list[widget_elem_count] = w_elem;
				widget_elem_count++;
				if (w_elem->id == SND_SOC_TPLG_DAPM_SCHEDULER)
					sched_found = 1;
			}
			/* some widget groups don't have scheduler... */
			if (sched_found) {
				pipeline_comp_count[hdr->index] = hdr->count;
				pipeline_id[pipeline_count] = hdr->index;
				pipeline_count++;
				sched_found = 0;
			}
			break;
		case SND_SOC_TPLG_TYPE_DAI_LINK:
		case SND_SOC_TPLG_TYPE_BACKEND_LINK:
			for (i = 0; i < hdr->count; i++) {
				printf("found link elem\n");
				ret = create_link_config(&link_config_elem,
							 tplg,
							 &i);
				if (ret != 0) {
					printf("error: reading link config\n");
					return -1;
				}
				link_config_elem_list[link_config_elem_count] =
					link_config_elem;
				link_config_elem_count++;
				/* print info */
				struct sof_ipc_dai_config config;
				ret = sof_link_load(&config,
						    link_config_elem);
			}
			break;
		case SND_SOC_TPLG_TYPE_PCM:
			for (i = 0; i < hdr->count; i++) {
				printf("found pcm elem\n");
				ret = create_pcm(&pcm_elem, tplg);
				if (ret != 0) {
					printf("error: reading pcm\n");
					return -1;
				}
				pcm_elem_list[pcm_elem_count] = pcm_elem;
				pcm_elem_count++;
				/* print info */
				sof_pcm_load(pcm_elem);
			}
			break;
		case SND_SOC_TPLG_TYPE_MANIFEST:
			printf("found manifest\n");
			ret = create_manifest(&manifest_elem, tplg);
			if (ret != 0) {
				printf("error: reading manifest\n");
				return -1;
			}
			manifest_elem_list[manifest_elem_count] = manifest_elem;
			manifest_elem_count++;
			/* print info */
			sof_manifest_load(manifest_elem);
			break;
		case SND_SOC_TPLG_TYPE_DAI:
			dai_elem = (struct snd_soc_tplg_dai*)tplg->pos;
			tplg->pos += dai_elem->size + dai_elem->priv.size;
			break;
		case SND_SOC_TPLG_TYPE_PDATA:
			printf("hdr type pdata, count is %d\n", hdr->count);
			break;
		case SND_SOC_TPLG_TYPE_MIXER:
			printf("hdr type mixer, count is %d\n", hdr->count);
			break;
		case SND_SOC_TPLG_TYPE_BYTES:
			printf("hdr type bytes, count is %d\n", hdr->count);
			break;
		case SND_SOC_TPLG_TYPE_ENUM:
			printf("hdr type enum, count is %d\n", hdr->count);
			break;

		default:
			printf("unknown hdr type, data pos %p total size %p\n",
			       (void*)tplg->pos,
			       (void*)(tplg->data + tplg->size));
			return 0;
		}

		if (tplg->pos >= tplg->data + tplg->size)
			return 0;
	}
}

void print_dapm_statistics()
{
	int i = 0;

	printf("************************************\n");
	printf("dapm statistics:\n\n");

	printf("dapm component count:\n");
	printf("header: %d\n", hdr_elem_count);
	printf("graph: %d\n", graph_elem_count);
	printf("widget: %d\n", widget_elem_count);
	printf("manifest: %d\n", manifest_elem_count);
	printf("pcm: %d\n", pcm_elem_count);
	printf("linkconfig: %d\n", link_config_elem_count);
	printf("dai: %d\n\n", dai_elem_count);

	for (i = 0; i < pipeline_count; i++) {
		printf("pipeline %d id %d component count %d\n",
		       i + 1, pipeline_id[i],
		       pipeline_comp_count[pipeline_id[i]]);
	}

	printf("************************************\n");
	printf("\n");
}

int get_pipeline_id(int comp_id)
{
	int i = 1;
	int pipeline_num = 0;

	while(i <= comp_id) {
		if (pipeline_num < pipeline_count) {
			i += pipeline_comp_count[pipeline_id[pipeline_num]];
			pipeline_num++;
		}
		else
			return -1;
	}

	return pipeline_num;
}

int find_widget_by_name(char* name)
{
	int i = 0;

	for (i = 0; i < widget_elem_count; i++) {
		if (!strcmp(name, widget_elem_list[i]->name)) {
			return 1;
		}
	}

	return 0;
}

int find_widget_by_stream_name(char* name)
{
	int i = 0;

	for (i = 0; i < widget_elem_count; i++) {
		if (widget_elem_list[i]->id == SND_SOC_TPLG_DAPM_AIF_IN ||
		    widget_elem_list[i]->id == SND_SOC_TPLG_DAPM_AIF_OUT) {
			if (!strcmp(name, widget_elem_list[i]->sname)) {
				return 1;
			}
		}
	}

	return 0;
}

int find_buffer_by_name(char* name)
{
	int i = 0;

	for (i = 0; i < widget_elem_count; i++) {
		if (!strcmp(name, widget_elem_list[i]->name)) {
			if (widget_elem_list[i]->id ==
			    SND_SOC_TPLG_DAPM_BUFFER) {
				return 1;
			}
		}
	}

	return 0;
}

int check_buffer_range(struct snd_soc_tplg_dapm_widget *widget_elem,
		       int pipeline_id, int component_id)
{
	int ret = 0;
	struct sof_ipc_buffer buffer;
	ret = sof_widget_load_buffer(&buffer, pipeline_id, component_id,
				     widget_elem);

	if (ret != 0) {
		printf("error: parse buffer tokens failed\n");
		return ret;
	}

	if (buffer.size < BUFFER_MIN_SIZE ||
	    buffer.size > BUFFER_MAX_SIZE) {
		printf("buffer range warning:\n");
		printf("buffer size value %d\n", buffer.size);
		ret++;
	}

	return ret;
}

int check_pipeline_range(struct snd_soc_tplg_dapm_widget *widget_elem,
			 int pipeline_id, int component_id)
{
	int ret = 0;
	struct sof_ipc_pipe_new pipe;
	ret = sof_widget_load_pipeline(&pipe, pipeline_id, component_id,
				       widget_elem);

	if (ret != 0) {
		printf("error: parse pipeline tokens failed\n");
		return ret;
	}

#if 0
	if (pipe.deadline < PIPE_MIN_DEADLINE ||
	    pipe.deadline > PIPE_MAX_DEADLINE) {
		printf("pipeline range warning:\n");
		printf("pipeline deadline value %d\n",
		       pipe.deadline);
		ret++;
	}
	if (pipe.priority < PIPE_MIN_PRIORITY ||
	    pipe.deadline > PIPE_MAX_PRIORITY) {
		printf("pipeline range warning:\n");
		printf("pipeline priority value %d\n",
		       pipe.priority);
		ret++;
	}
	if (pipe.mips < PIPE_MIN_MIPS ||
	    pipe.mips > PIPE_MAX_MIPS) {
		printf("pipeline range warning:\n");
		printf("pipeline mips value %d\n",
		       pipe.mips);
		ret++;
	}
	if (pipe.core < PIPE_MIN_CORE ||
	    pipe.core > PIPE_MAX_CORE) {
		printf("pipeline range warning:\n");
		printf("pipeline core value %d\n",
		       pipe.core);
		ret++;
	}
	if (pipe.frames_per_sched < PIPE_MIN_FRAMESPERSCHED ||
	    pipe.frames_per_sched > PIPE_MAX_FRAMESPERSCHED) {
		printf("pipeline range warning:\n");
		printf("pipeline frames_per_sched value %d\n",
		       pipe.frames_per_sched);
		ret++;
	}
#endif

	return ret;
}

int check_ssp_range(struct sof_ipc_dai_config *config)
{
	int ret = 0;

	if (!config)
		return -1;

#if 0
	printf("SSP range check\n");
	if(config.ssp.mclk_rate < SSP_MIN_MCLK ||
	   config.ssp.mclk_rate > SSP_MAX_MCLK) {
		printf("SSP range warning:\n");
		printf("mclk %d \n",  config.ssp.mclk_rate);
	}
	if(config.ssp.bclk_rate	< SSP_MIN_BCLK ||
	   config.ssp.bclk_rate > SSP_MAX_BCLK) {
		printf("SSP range warning:\n");
		printf("bclk %d \n", config.ssp.bclk_rate);
	}
	if(config.ssp.fsync_rate < SSP_MIN_FSYNC ||
	   config.ssp.fsync_rate > SSP_MAX_FSYNC) {
		printf("SSP range warning:\n");
		printf("fclk %d \n",  config.ssp.fsync_rate);
	}
	if(config.ssp.sample_valid_bits < SSP_MIN_VALIDBITS ||
	   config.ssp.sample_valid_bits > SSP_MAX_VALIDBITS) {
		printf("SSP range warning:\n");
		printf("valid bits %d\n", config.ssp.sample_valid_bits,
		       config.ssp.tdm_slot_width);
	}
	if(config.ssp.tdm_slot_width < SSP_MIN_TDMSLOTWIDTH ||
	   config.ssp.tdm_slot_width > SSP_MAX_TDMSLOTWIDTH) {
		printf("SSP range warning:\n");
		printf("width %d \n", config.ssp.tdm_slot_width);
	}
	if(config.ssp.tdm_slots < SSP_MIN_TDMSLOTS ||
	   config.ssp.tdm_slots > SSP_MAX_TDMSLOTS) {
		printf("SSP range warning:\n");
		printf("slots %d \n", config.ssp.tdm_slots);
	}
#endif

	/* General checks for all platform */

	if (config->ssp.bclk_rate > config->ssp.mclk_rate) {
		printf("link range warning:\n");
		printf("SSP bclk %d bigger than mckl %d\n", 
		config->ssp.bclk_rate, config->ssp.mclk_rate);
		ret++;
	}

	/* BCLK is generated from MCLK - must be divisable */
	if (config->ssp.mclk_rate % config->ssp.bclk_rate) {
		printf("link range warning:\n");
		printf("SSP mclk %d not divisable with bclk %d\n", 
		config->ssp.mclk_rate, config->ssp.bclk_rate);
		ret++;
	}

	/* calc frame width based on BCLK and rate - must be divisable */
	if (config->ssp.bclk_rate % config->ssp.fsync_rate) {
		printf("link range warning:\n");
		printf("SSP bclk %d not divisable with fsync %d\n",
		config->ssp.bclk_rate, config->ssp.fsync_rate);
		ret++;
	}

	/* must be enouch BCLKs for data */
	uint32_t bdiv = config->ssp.bclk_rate / config->ssp.fsync_rate;
	if (bdiv < config->ssp.tdm_slot_width *
	    config->ssp.tdm_slots) {
		printf("link range warning:\n");
		printf("SSP not enough bclk for data\n");
		ret++;
	}

	/* tdm_slot_width must be <= 38 for SSP */
	if (config->ssp.tdm_slot_width > 38) {
		printf("link range warning:\n");
		printf("SSP slot width bigger than 38\n");
		ret++;
	}

	return ret;
}

int check_dmic_range(struct sof_ipc_dai_config *config)
{
	int ret = 0;

	if (!config)
		return -1;

	if (config->dmic.pdmclk_min < DMIC_MIN_PDMCLK ||
	    config->dmic.pdmclk_min > config->dmic.pdmclk_max) {
		printf("DMIC range warning:\n");
		printf("pdmclk_min %d\n", config->dmic.pdmclk_min);
		ret++;
	}
	if (config->dmic.pdmclk_max > DMIC_MAX_PDMCLK) {
		printf("DMIC range warning:\n");
		printf("pdm_clkmax %d\n", config->dmic.pdmclk_max);
		ret++;
	}
	if (config->dmic.duty_min < DMIC_MIN_DUTY ||
	    config->dmic.duty_min > config->dmic.duty_max) {
		printf("DMIC range warning:\n");
		printf("duty_min %hd\n", config->dmic.duty_min);
		ret++;
	}
	if (config->dmic.duty_max > DMIC_MAX_DUTY) {
		printf("DMIC range warning:\n");
		printf("duty_max %hd\n", config->dmic.duty_max);
		ret++;
	}
	if (config->dmic.fifo_fs_a < DMIC_MIN_FIFOSFA ||
	    config->dmic.fifo_fs_a > DMIC_MAX_FIFOSFA) {
		printf("DMIC range warning:\n");
		printf("fifo_fs %d\n", config->dmic.fifo_fs_a);
		ret++;
	}
	if (config->dmic.num_pdm_active < DMIC_MIN_PDMACTIVE ||
	    config->dmic.num_pdm_active > DMIC_MAX_PDMACTIVE) {
		printf("DMIC range warning:\n");
		printf("num_pdms active %d\n",
		       config->dmic.num_pdm_active);
		ret++;
	}
	if (config->dmic.fifo_bits_a < DMIC_MIN_FIFOBITSA ||
	    config->dmic.fifo_bits_a > DMIC_MAX_FIFOBITSA) {
		printf("DMIC range warning:\n");
		printf("fifo word length %hd\n",
		       config->dmic.fifo_bits_a);
		ret++;
	}

	return ret;
}

int check_link_range(struct snd_soc_tplg_link_config* link_config_elem)
{
	int ret = 0;
	struct sof_ipc_dai_config config;

	ret = sof_link_load(&config, link_config_elem);

	if (ret != 0) {
		printf("error: parse link tokens failed\n");
		return ret;
	}

	switch (config.type) {
	case SOF_DAI_INTEL_SSP:
		ret = check_ssp_range(&config);
		break;
	case SOF_DAI_INTEL_DMIC:
		ret = check_dmic_range(&config);
		break;
	case SOF_DAI_INTEL_HDA:
		printf( "hda link range checking not supprted\n");
		break;
	default:
		printf("error: invalid DAI type in range check %d\n",
		       config.type);
		ret = -1;
		break;
	}

	return ret;
}

int check_range()
{
	struct snd_soc_tplg_dapm_widget *widget_elem;
	struct snd_soc_tplg_link_config *link_config_elem;
	int i = 0;
	int ret_buf = 0;
	int ret_pipe = 0;
	int ret_link = 0;

	printf("START RANGE CHECK\n");

	/* check pipeline parameters */
	/* check buffer parameters */
	for (i = 0; i < widget_elem_count; i++) {
		widget_elem = widget_elem_list[i];
		switch (widget_elem->id) {

		case SND_SOC_TPLG_DAPM_BUFFER:
			ret_buf += check_buffer_range(widget_elem,
						      get_pipeline_id(i+1),
						      i+1);
			break;
		case SND_SOC_TPLG_DAPM_SCHEDULER:
			ret_pipe += check_pipeline_range(widget_elem,
							 get_pipeline_id(i+1),
							 i+1);
			break;
		default:
			break;
		}
	}

	/* check link params like ssp, dmic etc. */
	for (i = 0; i < link_config_elem_count; i++) {
		link_config_elem = link_config_elem_list[i];
		ret_link += check_link_range(link_config_elem);
	}

	if (ret_buf == 0) {
		printf("no buffer range warnings!\n");
	}
	if (ret_pipe == 0) {
		printf("no pipeline range warnings!\n");
	}
	if (ret_link == 0) {
		printf("no link range warnings!\n");
	}

	printf("END OF RANGE CHECK\n\n");
	return 0;
}

int check_graph() {
	int i = 0, ret = 0;
	struct snd_soc_tplg_dapm_graph_elem *graph_elem;

	printf("START GRAPH CHECK\n");

	for (i = 0; i < graph_elem_count; i++) {
		graph_elem = graph_elem_list[i];

		/* check that graph components exist */
		if (find_widget_by_name(graph_elem->sink) &&
		    find_widget_by_stream_name(graph_elem->sink)) {
			printf("graph check warning: ");
			printf("no component named %s\n", graph_elem->sink);
			ret++;
		}
		if(find_widget_by_name(graph_elem->source) &&
		   find_widget_by_stream_name(graph_elem->source)) {
			printf("graph check warning: ");
			printf("no component named %s\n", graph_elem->source);
			ret++;
		}

		/* check that all components are connected to buffers */
		if (find_buffer_by_name(graph_elem->sink) &&
		    find_buffer_by_name(graph_elem->source)) {
			/* pcm can have "connection" to stream name */
			if (find_widget_by_stream_name(graph_elem->sink) &&
			    find_widget_by_stream_name(graph_elem->source))
				printf("graph check warning: ");
			printf("no buffer in connection %s to %s\n",
			       graph_elem->source, graph_elem->sink);
			ret++;
		}
	}

#if 0
	/* check that all pipelines have start and end */
	int j = 0, comp_c = 0;
	for (i = 0; i < pipeline_count; i++) {
		for (j = 0; j < pipeline_comp_count[pipeline_id[i]]; j++) {
			comp_c++;
		}
		widget_elem_list[comp_c];
	}
#endif

	if (ret == 0) {
		printf("no graph check warnings!\n");
	}
	printf("END OF GRAPH CHECK\n\n");

	return 0;
}

int check_ids() {
	int i = 0, j = 0;

	printf("START ID CHECK\n");

	for (i = 0; i < widget_elem_count; i++) {
		if (widget_elem_list[i]->id == SND_SOC_TPLG_DAPM_DAI_IN ||
		    widget_elem_list[i]->id == SND_SOC_TPLG_DAPM_DAI_OUT) {
			struct sof_ipc_comp_dai comp_dai;
			sof_widget_load_dai(&comp_dai, get_pipeline_id(i+1),
					    i+1, widget_elem_list[i]);
			printf("info: widget %s dai index %d should match the sof firmware platform specific 'struct dai' array index\n", 
			widget_elem_list[i]->name, comp_dai.dai_index);
		}
		else if (widget_elem_list[i]->id == SND_SOC_TPLG_DAPM_AIF_IN) {
			struct sof_ipc_comp_host host;
			sof_widget_load_pcm(&host, get_pipeline_id(i+1), i+1,
					    SOF_IPC_STREAM_PLAYBACK,
					    widget_elem_list[i]);
		}
		else if (widget_elem_list[i]->id == SND_SOC_TPLG_DAPM_AIF_OUT) {
			struct sof_ipc_comp_host host;
			sof_widget_load_pcm(&host, get_pipeline_id(i+1), i+1,
					    SOF_IPC_STREAM_CAPTURE,
					    widget_elem_list[i]);
		}
	}

	/* check that pcm's have unique id */
	for (i = 0; i < pcm_elem_count; i++) {
		for (j = i + 1; j < pcm_elem_count; j++) {
        	if (pcm_elem_list[i]->dai_id == pcm_elem_list[j]->dai_id) {
				printf("id check warning\n");
				printf("same id for pcm %s and pcm %s\n", 
				pcm_elem_list[i]->pcm_name, pcm_elem_list[j]->pcm_name);
        	}
    }
		sof_pcm_load(pcm_elem_list[i]);
	}

	for (i = 0; i < link_config_elem_count; i++) {
		struct sof_ipc_dai_config config;
		sof_link_load(&config, link_config_elem_list[i]);
		printf("info: link config %s id %d should match the 'struct snd_soc_dai_link.id' in kernel machine driver\n", 
		link_config_elem_list[i]->name, link_config_elem_list[i]->id);
	}

	printf("no id check warnings!\n");
	printf("END OF ID CHECK\n\n");

	return 0;
}

void usage(char *name)
{
	printf("usage: tplg_check -i <tplg file> -v\n");
	printf("usage: you need to specify at least input file\n");
	printf("usage: -v is optional parameter for printing more info\n");
}

int main(int argc, char **argv)
{
	struct tplg tplg_data;
	char *filename = 0;
	char *platname = 0;
	int opt = 0;
	size_t file_size;
	FILE *file;

	while ((opt = getopt(argc, argv, "i:hv")) != -1) {
		switch (opt) {
		case 'i':
			filename = optarg;
			break;
		case 'v':
			printf("getting info\n");
			print_info = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return -1;
		}
	}

	if (!filename) {
		usage(argv[0]);
		return -1;
	}

	/* open file */
	file = fopen(filename, "rb");
	if (!file) {
		fprintf(stderr, "Unable to open file %s", argv[1]);
		return -1;
	}

	/* file size */
	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Allocate memory */
	tplg_data.data = (unsigned char *)malloc(file_size);
	if (!tplg_data.data) {
		printf("error: mem alloc\n");
		return -1;
	}

	/* read the file into memory */
	fread(tplg_data.data, sizeof(unsigned char), file_size, file);
	tplg_data.size = file_size;
	tplg_data.pos = tplg_data.data;

	fclose(file);

	/* parse dapm elements to arrays from where we can check them later */
	printf("PARSING DAPM ELEMENTS\n\n");
	if (parse_dapm(&tplg_data) != 0) {
		printf("error: dapm parse\n");
		return -1;
	}
	print_dapm_statistics();

	/* do checks */
	check_range();
	check_graph();
	check_ids();

	return 0;
}
