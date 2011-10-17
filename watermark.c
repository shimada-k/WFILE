#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bitops.h"
#include "wfile.h"


#define WORD_FILE	"./words/obama.txt"

/*
	pathのファイルに書かれているテキストのバイト数を返す関数
	@path ファイルのパス
	return バイト数
*/
long getWordLength(const char *path)
{
	long sz;
	FILE *fp = fopen(path, "rb"); 
 
	/* ファイルサイズを調査 */ 
	fseek(fp, 0, SEEK_END); 
	sz = ftell(fp);
 
	fclose(fp);
 
	return sz;
}

/*
	テキストファイルを読み込み、引数のメモリ領域にコピーする関数
	@buf コピーする先のメモリ
	@count コピーするサイズ
	return コピーしたバイト数
*/
int readWords(char *buf, size_t count)
{
	FILE *f;
	int c, idx = 0;

	f = fopen(WORD_FILE, "r");

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
	char *path, *words = NULL;
	long wlength = 0;
	int mode = -1;

	/* 引数解析 */
	if(argc == 3){
		if(strcmp(argv[1], "-r") == 0){	/* 透かしを読むモード */
			mode = MODE_READ;
			path = argv[2];
			wlength = (unsigned int)getWordLength(WORD_FILE);
		}
		else if(strcmp(argv[1], "-w") == 0){	/* 透かしを書くモード */
			mode = MODE_WRITE;
			path = argv[2];
			wlength = (unsigned int)getWordLength(WORD_FILE);
		}
	}

	/* 引数違反 */
	if(mode == -1){
		puts("bad feeling");
		return 0;
	}

	if((words = calloc(wlength, sizeof(char))) == NULL){
		puts("calloc err");
	}

	if(mode == MODE_READ){	/* 透かしを読むモード */
		if((wmfp = wopen(path, "r")) == NULL){
			puts("wmf_open err");
		}

		wread(words, wlength, wmfp);

		wclose(wmfp);
		printf("%s\n", words);	/* 読み込んだ透かしを表示 */
	}
	else if(mode == MODE_WRITE){	/* 透かしを書くモード */

		if((wmfp = wopen(path, "a")) == NULL){
			puts("wmf_open err");
		}

		readWords(words, wlength);

		printf("%lu charctor is read\n", wlength);

		wwrite(words, wlength, wmfp);

		wclose(wmfp);
	}

	free(words);

	return 0;
}

