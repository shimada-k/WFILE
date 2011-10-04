#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gd.h>

#include "bitops.h"
#include "wmf.h"

int readWords(char *buf, size_t count)
{
	FILE *f;
	int c, idx = 0;

	f = fopen("./words/obama.txt", "r");

	while((c = fgetc(f)) != EOF){
		buf[idx] = (char)c;
		idx++;
	}

	fclose(f);

	return idx;
}

int main(int argc, char *argv[])
{
	WFILE *wmfp = NULL;
	char *words = NULL;
	int water_mark_length = 0;
	int mode = -1;

	/* 引数解析 */
	if(argc == 2){
		if(strcmp(argv[1], "-r") == 0){	/* 透かしを読むモード */
			mode = 1;
		}
		else if(strcmp(argv[1], "-w") == 0){	/* 透かしを書くモード */
			mode = 2;
		}
	}

	/* 引数違反 */
	if(mode == -1){
		puts("bad feeling");
		return 0;
	}

	//if((words = calloc(1026, sizeof(char))) == NULL){
	if((words = calloc(13502, sizeof(char))) == NULL){
		puts("calloc err");
	}

	if(mode == 1){	/* 透かしを読むモード */
		if((wmfp = wopen("write_test.png", "r")) == NULL){
			puts("wmf_open err");
		}
		//wmf_read(words, 1026, wmfp);
		wread(words, 13502, wmfp);

		wclose(wmfp);
	}
	else if(mode == 2){	/* 透かしを書くモード */

		if((wmfp = wopen("write_test.png", "w")) == NULL){
			puts("wmf_open err");
		}

		//water_mark_length = readWords(words, 1026);
		water_mark_length = readWords(words, 13502);
		printf("%d charctor is read\n", water_mark_length);

		//wmf_write(words, 1026, wmfp);
		wwrite(words, 13502, wmfp);

		wclose(wmfp);
	}

	printf("%s\n", words);

	free(words);

	return 0;
}

