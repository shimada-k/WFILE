#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* strcmp(3), strcpy(3) */
#include <unistd.h>	/* access(2) */
#include <errno.h>	/* perror(3) */

#include "bitops.h"
#include "wfile.h"

#define COLOR_RED	0
#define COLOR_GREEN	1
#define COLOR_BLUE	2
#define COLOR_ALPHA	3

/*
	TODO
	エラー処理の追加（メモリの確保のところなど）
	
*/


/******************************
*
*	ライブラリ内部関数
*
*******************************/

#define	IS_READ(mode)		pick_nbit8(mode, 0)
#define	IS_WRITE(mode)	pick_nbit8(mode, 1)
#define	IS_CREATE(mode)	pick_nbit8(mode, 2)
#define	WRITE_OFF_END(mode)	pick_nbit8(mode, 3)

#ifdef DEBUG
/*
	WFILE->oftの内容を表示する関数
	@oft 表示するwoff構造体のアドレス
*/
void printOffset(const woff_t *oft)
{
	printf("[plane_no:%d (x:%d, y:%d), color:%d]\n", oft->plane_no, oft->x, oft->y, oft->color);
}
#endif


/*
	WFILEのオフセットから次に処理するべきバイトの内容を返す
	@stream 処理するWFILEエントリのポインタ
	return 処理するべきバイトの内容
*/
static png_byte getColorFromOft(WFILE *stream)
{
	png_byte *row, *ptr;

	row = stream->specs.row_pointers[stream->offset.y];

	ptr = &(row[stream->offset.x * 4]);

	return ptr[stream->offset.color];
}

/*
	WFILEのオフセットからvalの値をstream->imgに埋め込む
	@val 色の輝度値
	@stream 処理の対象のWFILEのポインタ
*/
static void setColorFromOft(png_byte val, WFILE *stream)
{
	png_byte *row, *ptr;

	row = stream->specs.row_pointers[stream->offset.y];

	ptr = &(row[stream->offset.x * 4]);

	ptr[stream->offset.color] = val;
}

/*
	stream->offsetを1ドット分だけ進める関数
	return 成功:0 失敗:-1
*/
static int wseek_dot(WFILE *stream)
{
	/* oftを更新（座標、色、ビットプレーンの番号） */
	if(stream->offset.color == COLOR_ALPHA){	/* RGBAの最後のカラーだったら */
		if(stream->offset.x == stream->specs.x_size - 1){	/* ビットプレーンの横軸のMAXまでいっていたら */
			if(stream->offset.y == stream->specs.y_size -1){	/* ビットプレーンの最後までいっていたら */
				stream->offset.y = 0;
				stream->offset.x = 0;

				if(stream->offset.plane_no == 7){
#ifdef DEBUG
					printOffset(&stream->offset);
					puts("wseek_dot:watermark size over-flowed");
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
static char wread_byte(WFILE *stream)
{
	int i, bit;
	png_byte color;
	char result = -1;	/* 透かしは文字なのでcharでいい(asciiコードにマイナスの値は無い) */

	if((stream->bs = openBitStream(NULL, 1, "w")) == NULL){	/* 書き込みモードでビットストリームをopen */
		puts("wread_byte:cannot open bitstream");
		return result;
	}

	for(i = 0; i < 8; i++){	/* 8ドット分ループ */
		color = getColorFromOft(stream);	/* ドットの値はunsigend char(0 - 255) */
		bit = pick_nbit8(color, stream->offset.plane_no);
#ifdef DEBUG
		printOffset(&stream->offset);
		printf("bit:%d, color:%d\n", bit, color);
#endif

		/* ビットストリームに書き込む */
		writeBitStream(stream->bs, bit);
		/* 1 dot分だけオフセットを進める */
		wseek_dot(stream);

	}

#ifdef DEBUG
	//printf("%d 0x%02x ", *(char *)stream->bs->raw_data, *(char *)stream->bs->raw_data);	/* 読み出す時は透かし(== ascii)なのでchar変数として読み出し */
#endif

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
static void wwrite_byte(char val, WFILE *stream)
{
	int i, bit;
	png_byte color;

	stream->bs = openBitStream(&val, 1, "r");	/* valをもとにビットストリームを作成 */

	for(i = 0; i < 8; i ++){	/* 8ドット分ループ */
		color = getColorFromOft(stream);	/* 処理すべきバイトを取得する */

		bit = readBitStream(stream->bs);

		if(bit == 1){
			set_nbit8((unsigned char *)&color, stream->offset.plane_no);
		}
		else if(bit == 0){
			clr_nbit8((unsigned char *)&color, stream->offset.plane_no);
		}

#ifdef DEBUG
		//print_binary8(color);
		//printOffset(&stream->offset);
		//printf("bit:%d, color:%d\n", bit, color);
#endif

		setColorFromOft(color, stream);

		/* 1 dot分だけオフセットを進める */
		wseek_dot(stream);
	}

	closeBitStream(stream->bs);
}

#if 1
/*
	PNGのテキストチャンクからオフセットを読み込み、offsetにセットする関数
	@specs 読み取るPNG-specs
	@offset セットするwoff_t
*/
static void getOffsetFromChunk(woff_t *offset, png_structp png_ptr, png_infop info_ptr)
{
	png_textp text_ptr;
	char *tp, buf[32];
	int num_comments, num_text;

	/* コメントの取得 */
	num_comments = png_get_text(png_ptr, info_ptr, &text_ptr, &num_text);

	/*
	*	テキストチャンクに埋め込まれるオフセットデータは
	*	「plane_no,x,y,color」の形式
	*/

	if(num_comments == 0){
		return;
	}

	printf("num_comments : %d\n", num_comments);

#ifdef DEBUG
	printf("[key words]\n%s\n", text_ptr->key);
	printf("[text body]\n%s\n", text_ptr->text);
#endif

	strncpy(buf, text_ptr->text, 32);

	tp = strtok(buf, ",");
	offset->plane_no = atoi(tp);

	tp = strtok(NULL, ",");
	offset->x = atoi(tp);

	tp = strtok(NULL, ",");
	offset->y = atoi(tp);

	tp = strtok(NULL, ",");
	offset->color = atoi(tp);
}

/*
	offsetを文字列化してPNGのテキストチャンクに埋め込む関数
	@specs セットするPNG-specs
	@offset 埋め込むwoff_t
*/
static void setOffsetToChunk(const woff_t *offset, png_structp png_ptr, png_infop info_ptr)
{
	png_textp text_ptr;
	char buf[32];

	text_ptr = (png_text *)malloc(sizeof(png_text));

	text_ptr->compression = PNG_TEXT_COMPRESSION_NONE;
	text_ptr->key = (char *)malloc(sizeof(char) * 16);
	text_ptr->text = (char *)malloc(sizeof(char) * 32);

	sprintf(buf, "%d,%d,%d,%d", offset->plane_no, offset->x, offset->y, offset->color);

	strcpy(text_ptr->key, "WaterMark offset");
	strcpy(text_ptr->text, buf);

	png_set_text(png_ptr, info_ptr, text_ptr, 1);

	/* メモリを解放する */
	free(text_ptr);
	free(text_ptr->key);
	free(text_ptr->text);
}
#endif

/*
	ファイルからspecsを作る関数
	@fname 読み込むファイルのパス
	@specs 格納先のアドレス
*/
static void readPngFile(const char *fname, WFILE *wmfp)
{
	char header[8];    // 8 is the maximum size that can be checked
	int y;
	/* open file and test for it being a png */
	FILE *fp = NULL;
	png_structp png_ptr;		/* PNG管理用構造体 */
	png_infop info_ptr;		/* PNG管理用構造体 */

	fp = fopen(fname, "rb");

	if(!fp){
		printf("[readPngFile] File %s could not be opened for reading\n", fname);
	}

	fread(header, 1, 8, fp);

	if(png_sig_cmp((png_bytep)header, 0, 8)){
		printf("[readPngFile] File %s is not recognized as a PNG file\n", fname);
	}

	/* 構造体の初期化 */
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if(png_ptr == NULL){	/* エラー処理 */
		puts("[readPngFile] png_create_read_struct failed");
	}

	info_ptr = png_create_info_struct(png_ptr);

	if(info_ptr == NULL){	/* エラー処理 */
		puts("[readPngFile] png_create_info_struct failed");
	}

	if(setjmp(png_jmpbuf(png_ptr))){
		puts("[readPngFile] Error during init_io");
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);

	png_read_info(png_ptr, info_ptr);

	/* offsetの設定 */
	if(wmfp->mode.split.write_pos_end){
		getOffsetFromChunk(&wmfp->offset, png_ptr, info_ptr);
	}
	else{
		/* offsetは先頭 */
		wmfp->offset.plane_no = 0;
		wmfp->offset.x = 0;
		wmfp->offset.y = 0;
		wmfp->offset.color = COLOR_RED;
	}

	/* 各種データの取得 */
	wmfp->specs.x_size = png_get_image_width(png_ptr, info_ptr);
	wmfp->specs.y_size = png_get_image_height(png_ptr, info_ptr);

	wmfp->specs.color_type = png_get_color_type(png_ptr, info_ptr);
	wmfp->specs.bit_depth = png_get_bit_depth(png_ptr, info_ptr);

	wmfp->specs.number_of_passes = png_set_interlace_handling(png_ptr);
	png_read_update_info(png_ptr, info_ptr);

	/* ここからバイト列の読み込み */
	if (setjmp(png_jmpbuf(png_ptr))){
		puts("[readPngFile] Error during read_image");
	}

	/* メモリを確保する これで配列でアクセスできるようにする */
	wmfp->specs.row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * wmfp->specs.y_size);

#ifdef DEBUG
	printf("png_get_rowbytes = %lu\n", png_get_rowbytes(png_ptr, info_ptr));
#endif

	/* メモリを確保 これで列単位でアクセスできるようになる */
	for (y = 0; y < wmfp->specs.y_size; y++){
		wmfp->specs.row_pointers[y] = (png_byte *)malloc(png_get_rowbytes(png_ptr, info_ptr));
	}

	png_read_image(png_ptr, wmfp->specs.row_pointers);

	fclose(fp);
}

/*
	文字列のモードからwfile_mode_tを返す関数
	@str wopen()呼び出し側が設定した文字列
	return wfile_mode_t変数
*/
static wfile_mode_t transrateStrToMode(const char *str)
{
	wfile_mode_t mode;

	mode.full = 0;

	if(strcmp(str, "r") == 0){
		mode.full = MODE_READ;
	}
	else if(strcmp(str, "r+") == 0){
		mode.full = MODE_READ_PLUS;
	}
	else if(strcmp(str, "w") == 0){
		mode.full = MODE_WRITE;
	}
	else if(strcmp(str, "w+") == 0){
		mode.full = MODE_WRITE_PLUS;
	}
	else if(strcmp(str, "a") == 0){
		mode.full = MODE_APPEND;
	}
	else if(strcmp(str, "a+") == 0){
		mode.full = MODE_APPEND_PLUS;
	}
	else{
		puts("Invalide wopen mode");
	}

#ifdef DEBUG
	printf("mode.full = 0x%02x\n", mode.full);
#endif

	return mode;
}

/*
	PNGファイルを書き込む関数
	@wmfp 扱っているWFILEのアドレス
*/
static void writePngFile(WFILE *wmfp)
{
	struct png_operation_specs *specs = &wmfp->specs;
	png_structp png_ptr;		/* PNG管理用構造体 */
	png_infop info_ptr;		/* PNG管理用構造体 */

	/* 構造体初期化 */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if(png_ptr == NULL){
		puts("[write_png_file] png_create_write_struct failed");
	}

	info_ptr = png_create_info_struct(png_ptr);

	if(info_ptr == NULL){
		puts("[write_png_file] png_create_info_struct failed");
	}

	if(setjmp(png_jmpbuf(png_ptr))){
		puts("[write_png_file] Error during init_io");
	}

	png_init_io(png_ptr, wmfp->out_fp);

	/* ヘッダを書き込む */
	if(setjmp(png_jmpbuf(png_ptr))){
		puts("[writePngFile] Error during writing header");
	}

	png_set_IHDR(png_ptr, info_ptr, specs->x_size, specs->y_size,
		specs->bit_depth, specs->color_type, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	/* オフセットを書き込む */
	setOffsetToChunk(&wmfp->offset, png_ptr, info_ptr);

	png_write_info(png_ptr, info_ptr);

	if(setjmp(png_jmpbuf(png_ptr))){
		puts("[writePngFile] Error during writing bytes");
	}

	png_write_image(png_ptr, specs->row_pointers);

	puts("write complete");

	/* end write */
	if(setjmp(png_jmpbuf(png_ptr))){
		puts("[writePngFile] Error during end of write");
	}

	png_write_end(png_ptr, NULL);
}

/******************************
*
*	公開用ライブラリ関数
*
*******************************/

/*
	WFILEのエントリを作成する関数
	return メモリを確保したWFILEのアドレス
*/
WFILE *wopen(const char *path, const char *str)
{
	WFILE *wmfp = NULL;

	if((wmfp = malloc(sizeof(WFILE))) == NULL){
		return NULL;
	}

	/* 文字列モードを変換 */
	wmfp->mode = transrateStrToMode(str);

	if(access(path, F_OK) == 0){	/* ファイルが存在する場合 */

		if(wmfp->mode.split.can_read && wmfp->mode.split.can_write == 0){	/* 読み込みだけ */
			/* pathからspecを作る */
			readPngFile(path, wmfp);
		}
		else if(wmfp->mode.split.can_read == 0 && wmfp->mode.split.can_write){	/* 書き込みだけ */
			if(wmfp->mode.split.can_trancate){	/* 長さを切り詰める場合 */
				/* もと画像からspecを作る */
				readPngFile(BASE_IMG, wmfp);
			}
			else{
				/* pathからspecを作る */
				readPngFile(path, wmfp);
			}
			/* 書き込み用にwmfp->out_fpもオープンしておく */
			wmfp->out_fp = fopen(path, "wb");
		}
		else if(wmfp->mode.split.can_read && wmfp->mode.split.can_write){	/* 読み込み＋書き込み */
			if(wmfp->mode.split.can_trancate){	/* 長さを切り詰める場合 */
				/* もと画像からspecを作る */
				readPngFile(BASE_IMG, wmfp);
			}
			else{
				readPngFile(path, wmfp);
			}
			/* 書き込み用にwmfp->out_fpもオープンしておく */
			wmfp->out_fp = fopen(path, "wb");
		}

	}
	else{	/* ファイルが存在しない場合 */
		if(wmfp->mode.split.can_create){	/* 新規作成できるなら作成する */
			/* もと画像からspecを作る */
			readPngFile(BASE_IMG, wmfp);

			wmfp->out_fp = fopen(path, "wb");
		}
		else{
			puts("err");
		}
	}

	return wmfp;
}

/*
	sizeバイトのデータを読み取り、ptrに格納する
	@ptr 読み込んだデータを格納するバッファ（@sizeのサイズが無いといけない）
	@size 読み込むサイズ（バイト）
	return 実際に読み込めたデータのバイト数
*/
size_t wread(void *ptr, size_t size, WFILE *stream)
{
	int i;
	char result;
	char *buf;
	size_t ret = 0;

	buf = (char *)ptr;

	for(i = 0; i < size; i++){	/* バイトごとにループを回す */
		if((result = wread_byte(stream)) == -1){
#ifdef DEBUG
			printf("result:%d, %d byte read\n", result, i);
#endif
			return ret;
		}
		else{
			buf[i] = result;
			ret++;
		}

		puts("piyo");
	}

	return ret;
}

/*
	prtからsizeバイトのデータをstreamに書込む関数
	return 実際に書き込めたデータの個数
*/
size_t wwrite(const void *ptr, size_t size, WFILE *stream)
{
	int i;
	size_t ret = 0;

	for(i = 0; i < size; i++){
		wwrite_byte(*(char *)(ptr + i), stream);
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
	int y;

	if(stream->mode.split.can_write){	/* 書込み許可なモードだとout_fpがオープンされている */
		writePngFile(stream);

		fclose(stream->out_fp);
	}

	/* specs内のメモリの解放 */
	for(y = 0; y < stream->specs.y_size; y++){
		free(stream->specs.row_pointers[y]);
	}

	free(stream->specs.row_pointers);
	free(stream);
}

