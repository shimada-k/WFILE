#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gd.h>

#include "bitops.h"
#include "wfile.h"

/*
	TODO
	・FUSEの開発環境をノートPCに構築する
	・明日の持ち物を洗い出す
	・ホテルの地図を印刷しておく
	
*/


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
	char *path, *words = NULL;
	long wlength = 0;
	int mode = -1;

	/* 引数解析 */
	if(argc == 2 || argc == 3){
		if(strcmp(argv[1], "-r") == 0){	/* 透かしを読むモード */
			mode = MODE_READ;
		}
		else if(strcmp(argv[1], "-w") == 0){	/* 透かしを書くモード */
			if(argc == 2){
				puts("bad feeling");	/* 引数エラー */
				return 0;
			}
			else{
				path = argv[2];
			}

			mode = MODE_WRITE;
			wlength = (unsigned int)getWordLength(path);
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
		if((wmfp = wopen("write_test.png", "r")) == NULL){
			puts("wmf_open err");
		}
		wread(words, wlength, wmfp);

		wclose(wmfp);
	}
	else if(mode == MODE_WRITE){	/* 透かしを書くモード */

		if((wmfp = wopen("write_test.png", "w")) == NULL){
			puts("wmf_open err");
		}

		readWords(words, wlength);

		printf("%d charctor is read\n", wlength);

		wwrite(words, wlength, wmfp);

		wclose(wmfp);
	}

	printf("%s\n", words);

	free(words);

	return 0;
}

