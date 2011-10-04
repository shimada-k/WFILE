#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gd.h>

#include "bitops.h"
#include "wmf.h"

#define COLOR_RED	0
#define COLOR_GREEN	1
#define COLOR_BLUE	2

/*
	TODO 2011/10/4
	・コードのリファクタリング
	・wmfp_openのモードを増やす
	・ビットプレーンにまたがった透かしを読み書きしてみる
	・bitops, wmfドキュメントの作成
*/


#ifdef DEBUG
/*
	WFILE->oftの内容を表示する関数
	@oft 表示するwoff構造体のアドレス
*/
void print_offset(const woff_t *oft)
{
	printf("[plane_no:%d (x:%d, y:%d), color:%d]\n", oft->plane_no, oft->x, oft->y, oft->color);
}
#endif

/*
	WFILEのオフセットから次に処理するべきバイトの内容を返す
	@stream 処理するWFILEエントリのポインタ
	return 処理するべきバイトの内容
*/
static unsigned char get_color_from_oft(WFILE *stream)
{
	int color;
	unsigned char result;

	color = gdImageGetPixel(stream->img, stream->offset.x, stream->offset.y);

	switch(stream->offset.color){
		case COLOR_RED:
			result = (unsigned char)gdImageRed(stream->img, color);
			break;
		case COLOR_GREEN:
			result = (unsigned char)gdImageGreen(stream->img, color);
			break;
		case COLOR_BLUE:
			result = (unsigned char)gdImageBlue(stream->img, color);
			break;
	}

	return result;
}

/*
	WFILEのオフセットからvalの値をstream->imgに埋め込む
	@val 色の輝度値
	@stream 処理の対象のWFILEのポインタ
*/
static void set_color_from_oft(unsigned char val, WFILE *stream)
{
	int color;
	unsigned char rgb[3];

	color = gdImageGetPixel(stream->img, stream->offset.x, stream->offset.y);

	rgb[COLOR_RED] = (unsigned char)gdImageRed(stream->img, color);
	rgb[COLOR_GREEN] = (unsigned char)gdImageGreen(stream->img, color);
	rgb[COLOR_BLUE] = (unsigned char)gdImageBlue(stream->img, color);

	/* エラー処理 stream->offset.colorが3以上の数値を指していないかどうか */

	rgb[stream->offset.color] = val;

	gdImageSetPixel(stream->img, stream->offset.x, stream->offset.y, gdTrueColor(rgb[COLOR_RED], rgb[COLOR_GREEN], rgb[COLOR_BLUE]));
}


/*
	stream->offsetを1ドット分だけ進める関数
	return 成功:0 失敗:-1
*/
static int wmf_seek_dot(WFILE *stream)
{

	/* oftを更新（座標、色、ビットプレーンの番号） */
	if(stream->offset.color == COLOR_BLUE){	/* RGBの最後のカラーだったら */
		if(stream->offset.x == stream->x_size - 1){	/* ビットプレーンの横軸のMAXまでいっていたら */
			if(stream->offset.y == stream->y_size -1){	/* ビットプレーンの最後までいっていたら */
				stream->offset.y = 0;
				stream->offset.x = 0;

				if(stream->offset.plane_no == 7){
#ifdef DEBUG
					print_offset(&stream->offset);
					puts("unassumption err");
#endif
					return -1;	/* 画像に埋め込める上限を越えたときは考慮されていない */
				}
				else{
					stream->offset.plane_no++;
				}
			}
			else{
				stream->offset.x = 0;
				stream->offset.y++;
			}
		}
		else{
			stream->offset.x++;
		}
		stream->offset.color = COLOR_RED;
	}
	else{	/* ピクセル内で色を変えるだけ(ピクセル位置は変えない) */
		stream->offset.color++;
	}

	return 0;
}

/*
	WFILEからビットストリームで1バイト読み込み、埋め込まれている透かしを返す関数
	@stream 処理するWFILEエントリ
	@return 成功：透かし1バイトの内容 失敗：-1
*/
static char wmf_read_byte(WFILE *stream)
{
	int i, bit;
	unsigned char color;
	char result = -1;	/* 透かしは文字なのでcharでいい(asciiコードにマイナスの値は無い) */

	if((stream->bs = openBitStream(NULL, 1, "w")) == NULL){	/* 書き込みモードでビットストリームをopen */
		puts("err openBitStream()");
		return result;
	}

	for(i = 0; i < 8; i++){	/* 8ドット分ループ */
		color = get_color_from_oft(stream);	/* ドットの値はunsigend char(0 - 255) */
		bit = pick_nbit8(color, stream->offset.plane_no);
#ifdef DEBUG
		print_offset(&stream->offset);
		printf("bit:%d, color:%d\n", bit, color);
#endif

		/* ビットストリームに書き込む */
		writeBitStream(stream->bs, bit);
		/* 1 dot分だけオフセットを進める */
		wmf_seek_dot(stream);

	}

	printf("%d 0x%02x ", *(char *)stream->bs->raw_data, *(char *)stream->bs->raw_data);	/* 読み出す時は透かし(== ascii)なのでchar変数として読み出し */

	memcpy(&result, (char *)stream->bs->raw_data, 1);

	closeBitStream(stream->bs);	/* ビットストリームをclose */

	printf("result = %d(%c)\n", result, result);

	return result;
}

/*
	valの内容をstream->imgに埋め込む
	@val 透かしとして埋め込むバイトの内容
	@stream 処理対象のWFILEのアドレス
*/
static void wmf_write_byte(char val, WFILE *stream)
{
	int i, bit;
	unsigned char color;

	stream->bs = openBitStream(&val, 1, "r");	/* valをもとにビットストリームを作成 */

	for(i = 0; i < 8; i ++){	/* 8ドット分ループ */
		color = get_color_from_oft(stream);	/* 処理すべきバイトを取得する */

		bit = readBitStream(stream->bs);

		if(bit == 1){
			set_nbit8(&color, stream->offset.plane_no);
		}
		else if(bit == 0){
			clr_nbit8(&color, stream->offset.plane_no);
		}

#ifdef DEBUG
		//print_binary8(color);
		print_offset(&stream->offset);
		printf("bit:%d, color:%d\n", bit, color);
#endif

		set_color_from_oft(color, stream);

		/* 1 dot分だけオフセットを進める */
		wmf_seek_dot(stream);
	}

	closeBitStream(stream->bs);
}

/*
	WFILEのエントリを作成する関数
	return メモリを確保したWFILEのアドレス
*/
WFILE *wopen(const char *path, const char *mode)
{
	FILE *fp = NULL;
	WFILE *wmfp = NULL;

	if((wmfp = malloc(sizeof(WFILE))) == NULL){
		return NULL;
	}

	/* エラー処理、pathが「.png」で終わっているか */

	/* とりあえず2種類のモードだけ実装 */
	if(strcmp(mode, "r") == 0){	/* 読み込みモード */
		if((fp = fopen(path, mode)) == NULL){
#ifdef DEBUG
			printf("%s is not found\n", path);
#endif
			return NULL;
		}

		wmfp->img = gdImageCreateFromPng(fp);
		wmfp->x_size = gdImageSX(wmfp->img);
		wmfp->y_size = gdImageSY(wmfp->img);
		wmfp->mode = MODE_READ;

#ifdef DEBUG
		printf("%s (%d, %d)\n", path, wmfp->x_size, wmfp->y_size);
#endif

		wmfp->bs = NULL;

		wmfp->offset.plane_no = 0;
		wmfp->offset.x = 0;
		wmfp->offset.y = 0;
		wmfp->offset.color = COLOR_RED;
	}
	else if(strcmp(mode, "w") == 0){	/* 書き込みモード */

		if((fp = fopen("./img/logo_mini.png", "r")) == NULL){
#ifdef DEBUG
			printf("%s is not found\n", path);
#endif
			return NULL;
		}
		wmfp->img = gdImageCreateFromPng(fp);

		if((wmfp->fp = fopen(path, "wb")) == NULL){	/* これは画像ファイルが出力されるのでwbモード */
#ifdef DEBUG
			printf("%s is not found\n", path);
#endif
			return NULL;
		}

		wmfp->x_size = gdImageSX(wmfp->img);
		wmfp->y_size = gdImageSY(wmfp->img);
		wmfp->mode = MODE_WRITE;

		wmfp->bs = NULL;

		wmfp->offset.plane_no = 0;
		wmfp->offset.x = 0;
		wmfp->offset.y = 0;
		wmfp->offset.color = COLOR_RED;
	}
	else{
		return NULL;
	}

	if(fp == NULL){
		return NULL;
	}
	else{
		fclose(fp);
	}

	return wmfp;
}

/*
	sizeバイトのデータを読み取り、ptrに格納する
	return 実際に読み込めたデータのバイト数
	@ptr 読み込んだデータを格納するバッファ（@sizeのサイズが無いといけない）
	@size 読み込むサイズ（バイト）
 */
size_t wread(void *ptr, size_t size, WFILE *stream)
{
	int i;
	char result;
	size_t ret = 0;
	char *buf;

	buf = (char *)ptr;

	for(i = 0; i < size; i++){	/* バイトごとにループを回す */
		if((result = wmf_read_byte(stream)) == -1){
#ifdef DEBUG
			printf("result:%d, %d byte read\n", result, i);
#endif
			break;
		}
		else{
			buf[i] = result;
			ret++;
		}
	}

	return ret;
}

/*
	prtからsizeバイトのデータをstreamに書込む
	return 実際に書き込めたデータの個数
*/
size_t wwrite(const void *ptr, size_t size, WFILE *stream)
{
	int i;
	size_t ret = 0;

	for(i = 0; i < size; i++){
		wmf_write_byte(*(char *)(ptr + i), stream);
		ret++;
	}

	return ret;
}

/*
	WFILEのエントリのメモリデータを解放する関数
	@stream 
*/
void wclose(WFILE *stream)
{
	if(stream->mode == MODE_WRITE){
		gdImagePng(stream->img, stream->fp);
		fclose(stream->fp);
	}

	gdImageDestroy(stream->img);

	free(stream);
}

