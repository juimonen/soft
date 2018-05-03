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
#include <sound/asoc.h>
#include <string.h>
#include <stdlib.h>

/*
 * The output graph is saved as "tplg.png"
 * Usage: ./tplg_graph <tplg file>
 */

int main(int argc, char **argv)
{
	struct snd_soc_tplg_hdr *hdr;
	struct snd_soc_tplg_dapm_graph_elem *graph_elem;
	char *filename;
	char *dotfilename = "tplg.dot";
	FILE *file, *dotfile;
	char *start = "digraph topology {\n";
	char *node_color = "node [color=Red,fontname=Courier]\n";
	char *edge_color = "edge [color=Blue, style=dashed]\n";
	char *end = "}\n";
	char edge[256];
	int i, status, ret;
	size_t size, file_size;

	/* command line arguments */
	if (argc < 2) {
		printf("usage: tplg_graph <tplg file>\n");
		exit(EXIT_FAILURE);
	}

	filename = malloc(strlen(argv[1]));
	strcpy(filename, argv[1]);

	/* open files */
	file = fopen(filename, "rb");
	if (!file) {
		fprintf(stderr, "Unable to open file %s", filename);
		return -1;
	}

	dotfile = fopen(dotfilename, "w");
	if (!dotfile) {
		fprintf(stderr, "Unable to open file %s", dotfilename);
		return -1;
	}

	fwrite(start, strlen(start), 1, dotfile);
	fwrite(node_color, strlen(node_color), 1, dotfile);
	fwrite(edge_color, strlen(edge_color), 1, dotfile);

	/* file size */
	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	/* Allocate memory */
	size = sizeof(struct snd_soc_tplg_hdr);
	hdr = (struct snd_soc_tplg_hdr *)malloc(size);
	if (!hdr) {
		printf("error: mem alloc\n");
		return -1;
	}

	size = sizeof(struct snd_soc_tplg_dapm_graph_elem);
	graph_elem = (struct snd_soc_tplg_dapm_graph_elem *)malloc(size);
	if (!graph_elem) {
		printf("error: mem alloc\n");
		return -1;
	}

	while (1) {
		ret = fread(hdr, sizeof(struct snd_soc_tplg_hdr), 1, file);
		if (ret != 1)
			printf("error: reading header\n");

		switch (hdr->type) {
		case SND_SOC_TPLG_TYPE_DAPM_GRAPH:
			size = sizeof(struct snd_soc_tplg_dapm_graph_elem);
			/* parse pipeline graph */
			for (i = 0; i < hdr->count; i++) {
				ret = fread(graph_elem, size, 1, file);
				if (ret != 1)
					printf("error: reading graph elem\n");

				/* write route to output dot file */
				sprintf(edge, "\"%s\"->\"%s\"\n",
					graph_elem->source, graph_elem->sink);
				fwrite(edge, strlen(edge), 1, dotfile);
				fwrite("\n", 1, 1, dotfile);
			}

			if (ftell(file) == file_size)
				goto finish;
			break;

		default:
			fseek(file, hdr->payload_size, SEEK_CUR);
			if (ftell(file) == file_size)
				goto finish;
			break;
		}
	}

finish:
	fwrite(end, strlen(end), 1, dotfile);
	fclose(dotfile);
	fclose(file);
	free(hdr);
	free(graph_elem);

	/* create topology graph */
	status = system("dot tplg.dot -Tpng -o tplg.png");
	if (status < 0) {
		printf("error: creating tplg graph\n");
		exit(EXIT_FAILURE);
	}
}
