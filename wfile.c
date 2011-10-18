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
	画像のデータ分一気にメモリを確保するのではなく、渡されたデータ分だけ行を確保する
	画像中のあるrowだけ変更して書き込みは無理だった。
		行ごとに書き込めば？
		write_rowとかの関数を作って、wread()のsizeでループ
	
*/


/******************************
*
*	ライブラリ内部関数
*
*******************************/

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
	@wmfp 処理するWFILEエントリのポインタ
	return 処理するべきバイトの内容
*/
static png_byte getColorFromOft(WFILE *wmfp)
{
	png_byte *row, *ptr;

	row = wmfp->sspecs.row_pointers[wmfp->offset.y];

	//row = &wmfp->sspecs.row_pointers[wmfp->offset.y];

	ptr = &(row[wmfp->offset.x * 4]);

	return ptr[wmfp->offset.color];
}

/*
	WFILEのオフセットからvalの値をwmfp->imgに埋め込む
	@val 色の輝度値
	@wmfp 処理の対象のWFILEのポインタ
*/
static void setColorFromOft(png_byte val, WFILE *wmfp)
{
	png_byte *row, *ptr;

	row = wmfp->sspecs.row_pointers[wmfp->offset.y];

	//row = &wmfp->sspecs.row_pointers[wmfp->offset.y];

	ptr = &(row[wmfp->offset.x * 4]);

	ptr[wmfp->offset.color] = val;
}

/*
	wmfp->offsetを1ドット分だけ進める関数
	return 成功:0 失敗:-1
*/
static int wseek_dot(WFILE *wmfp)
{
	/* oftを更新（座標、色、ビットプレーンの番号） */
	if(wmfp->offset.color == COLOR_ALPHA){	/* RGBAの最後のカラーだったら */
		if(wmfp->offset.x == wmfp->sspecs.x_size - 1){	/* ビットプレーンの横軸のMAXまでいっていたら */
			if(wmfp->offset.y == wmfp->sspecs.y_size -1){	/* ビットプレーンの最後までいっていたら */
				wmfp->offset.y = 0;
				wmfp->offset.x = 0;

				if(wmfp->offset.plane_no == 7){
#ifdef DEBUG
					printOffset(&wmfp->offset);
					puts("wseek_dot:watermark size over-flowed");
#endif
					return -1;	/* 画像に埋め込める上限を越えたときは考慮されていない */
				}
				else{
					wmfp->offset.plane_no++;
				}
			}
			else{
				wmfp->offset.x = 0;
				wmfp->offset.y++;
			}
		}
		else{
			wmfp->offset.x++;
		}
		wmfp->offset.color = COLOR_RED;
	}
	else{	/* ピクセル内で色を変えるだけ(ピクセル位置は変えない) */
		wmfp->offset.color++;
	}

	return 0;
}

/*
	WFILEからビットストリームで1バイト読み込み、埋め込まれている透かしを返す関数
	@wmfp 処理するWFILEエントリ
	@return 成功：透かし1バイトの内容 失敗：-1
*/
static char wread_byte(WFILE *wmfp)
{
	int i, bit;
	png_byte color;
	char result = -1;	/* 透かしは文字なのでcharでいい(asciiコードにマイナスの値は無い) */

	if((wmfp->bs = openBitStream(NULL, 1, "w")) == NULL){	/* 書き込みモードでビットストリームをopen */
		puts("wread_byte:cannot open bitwmfp");
		return result;
	}

	for(i = 0; i < 8; i++){	/* 8ドット分ループ */
		color = getColorFromOft(wmfp);	/* ドットの値はunsigend char(0 - 255) */
		bit = pick_nbit8(color, wmfp->offset.plane_no);
#ifdef DEBUG
		printOffset(&wmfp->offset);
		printf("bit:%d, color:%d\n", bit, color);
#endif

		/* ビットストリームに書き込む */
		writeBitStream(wmfp->bs, bit);
		/* 1 dot分だけオフセットを進める */
		wseek_dot(wmfp);

	}

#ifdef DEBUG
	//printf("%d 0x%02x ", *(char *)wmfp->bs->raw_data, *(char *)wmfp->bs->raw_data);	/* 読み出す時は透かし(== ascii)なのでchar変数として読み出し */
#endif

	memcpy(&result, (char *)wmfp->bs->raw_data, 1);

	closeBitStream(wmfp->bs);	/* ビットストリームをclose */

	printf("result = %d(%c)\n", result, result);

	return result;
}

/*
	valの内容をwmfp->imgに埋め込む
	@val 透かしとして埋め込むバイトの内容
	@wmfp 処理対象のWFILEのアドレス
*/
static void wwrite_byte(char val, WFILE *wmfp)
{
	int i, bit;
	png_byte color;

	wmfp->bs = openBitStream(&val, 1, "r");	/* valをもとにビットストリームを作成 */

	for(i = 0; i < 8; i ++){	/* 8ドット分ループ */
		color = getColorFromOft(wmfp);	/* 処理すべきバイトを取得する */

		bit = readBitStream(wmfp->bs);

		if(bit == 1){
			set_nbit8((unsigned char *)&color, wmfp->offset.plane_no);
		}
		else if(bit == 0){
			clr_nbit8((unsigned char *)&color, wmfp->offset.plane_no);
		}

#ifdef DEBUG
		//print_binary8(color);
		//printOffset(&wmfp->offset);
		//printf("bit:%d, color:%d\n", bit, color);
#endif

		setColorFromOft(color, wmfp);	/* ここでrow_pointersに格納 */

		/* 1 dot分だけオフセットを進める */
		wseek_dot(wmfp);
	}

	closeBitStream(wmfp->bs);
}

#if 1
/*
	１行分のデータをwmfp->sspecs.row_pointersから読み込む関数
	@wmfp 対象のWFILEのアドレス
	@ptr 読み込んだ結果を格納するバッファ
	@size ptrへ格納するバイトサイズ
*/
void wread_row(WFILE *wmfp, void *ptr, size_t size)
{
	int i;

#ifdef DEBUG
	printf("Notice, End line size = %lu\n", size);	/* size < row_bytesだった時 */
#endif

	png_read_row(wmfp->r_cspecs.png_ptr, wmfp->sspecs.row_pointer, NULL);	/* 画像から1行分のデータをrow_pointerに読み込む */

	for(i = 0; i < size; i++){
		wread_byte(*(char *)(ptr + i), wmfp);	/* この中でshared_specsのrow_pointersに書き込んでいるのでローカルのpng_pointerに書き込む用にする */
	}
}

/*
	１行分のデータをwmfp->sspecs.row_pointersに書き込む関数
	@wmfp 対象のWFILEのアドレス
	@ptr 1行分のバイトデータ
	@size ptrからフェッチするサイズ
*/
void wwrite_row(WFILE *wmfp, void *ptr, size_t size)
{
	int i;

#ifdef DEBUG
	printf("Notice, End line size = %lu\n", size);	/* size < row_bytesだった時 */
#endif

	png_read_row(wmfp->r_cspecs.png_ptr, wmfp->sspecs.row_pointer, NULL);	/* 画像から1行分のデータをrow_pointerに読み込む */

	for(i = 0; i < size; i++){
		wwrite_byte(*(char *)(ptr + i), wmfp);	/* この中でshared_specsのrow_pointersに書き込んでいるのでローカルのpng_pointerに書き込む用にする */
	}

	png_write_row(wmfp->w_cspecs.png_ptr, wmfp->sspecs.row_pointer);	
}
#endif


#if 1
/*
	PNGのテキストチャンクからオフセットを読み込み、offsetにセットする関数
	@sspecs 読み取るPNG-sspecs
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
	@sspecs セットするPNG-sspecs
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

#define SIG_CHECK_SIZE	8	/* PNGシグネチャをチェックするバイト数（4?8） */

/*
	読み込み用のpng_control_specsを設定する関数
	@fname ファイル名
	@r_specs 設定するpng_control_specsのアドレス
*/
static void createReadCspecs(const char *fname, struct png_control_specs *cspecs)
{
	char header[SIG_CHECK_SIZE];    // 8 is the maximum size that can be checked
	int interlace_method, compression_method, filter_method;
	/* open file and test for it being a png */

	cspecs->fp = fopen(fname, "rb");

	if(cspecs->fp == NULL){
		printf("[readPngFile] File %s could not be opened for reading\n", fname);
	}

	fread(header, 1, SIG_CHECK_SIZE, cspecs->fp);

	if(png_sig_cmp((png_bytep)header, 0, SIG_CHECK_SIZE)){
		printf("[readPngFile] File %s is not recognized as a PNG file\n", fname);
	}

	/* 構造体の初期化 */
	cspecs->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if(cspecs->png_ptr == NULL){	/* エラー処理 */
		puts("[readPngFile] png_create_read_struct failed");
	}

	cspecs->info_ptr = png_create_info_struct(cspecs->png_ptr);

	if(cspecs->info_ptr == NULL){	/* エラー処理 */
		puts("[readPngFile] png_create_info_struct failed");
	}

	if(setjmp(png_jmpbuf(cspecs->png_ptr))){
		puts("[readPngFile] Error during init_io");
	}

	png_init_io(cspecs->png_ptr, cspecs->fp);
	png_set_sig_bytes(cspecs->png_ptr, SIG_CHECK_SIZE);

	png_read_info(cspecs->png_ptr, cspecs->info_ptr);

	/* 行バイト数、縦横サイズ、ビット深度、カラータイプなど取得 */
	cspecs->shared_specs->row_bytes = png_get_rowbytes(cspecs->png_ptr, cspecs->info_ptr);

	png_get_IHDR(cspecs->png_ptr, cspecs->info_ptr, &cspecs->shared_specs->x_size, &cspecs->shared_specs->y_size,
			&cspecs->shared_specs->bit_depth, &cspecs->shared_specs->color_type,
			&interlace_method, &compression_method, &filter_method);

	if(interlace_method){	/* インターレース方式を採用した画像は対象外 */
		puts("interlace is valid, not supported");
		return;
	}

	/* compression_method, filter_methodは使わない */
}

/*
	書き込み用のpng_control_specsを設定する関数
	@fname ファイル名
	@w_specs 設定するpng_control_specsのアドレス
*/
static void createWriteCspecs(const char *fname, struct png_control_specs *cspecs)
{
	/* open file and test for it being a png */

	cspecs->fp = fopen(fname, "wb");

	if(cspecs->fp == NULL){
		printf("[readPngFile] File %s could not be opened for reading\n", fname);
	}

	/* 構造体の初期化 */
	cspecs->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if(cspecs->png_ptr == NULL){	/* エラー処理 */
		puts("[readPngFile] png_create_read_struct failed");
	}

	cspecs->info_ptr = png_create_info_struct(cspecs->png_ptr);

	if(cspecs->info_ptr == NULL){	/* エラー処理 */
		puts("[readPngFile] png_create_info_struct failed");
	}

	png_init_io(cspecs->png_ptr, cspecs->fp);

	if(setjmp(png_jmpbuf(cspecs->png_ptr))){
		puts("[readPngFile] Error during init_io");
	}

	/* ヘッダに書き込む */
	png_set_IHDR(cspecs->png_ptr, cspecs->info_ptr,
			cspecs->shared_specs->x_size, cspecs->shared_specs->y_size,
			cspecs->shared_specs->bit_depth, cspecs->shared_specs->color_type,
			PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_write_info(cspecs->png_ptr, cspecs->info_ptr);
}

/*
	ファイルからsspecsを作る関数
	@fname 読み込むファイルのパス
	@wmfp 扱っているWMFILEのアドレス
*/
static void readPngFile(const char *fname, WFILE *wmfp)
{
	char header[SIG_CHECK_SIZE];    // 8 is the maximum size that can be checked
	int i, interlace_method, compression_method, filter_method;
	png_structp png_ptr;
	png_infop info_ptr;
	FILE *fp;
	/* open file and test for it being a png */

	fp = fopen(fname, "rb");

	if(fp == NULL){
		printf("[readPngFile] File %s could not be opened for reading\n", fname);
	}

	fread(header, 1, SIG_CHECK_SIZE, fp);

	if(png_sig_cmp((png_bytep)header, 0, SIG_CHECK_SIZE)){
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
	png_set_sig_bytes(png_ptr, SIG_CHECK_SIZE);

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

	/* 縦横サイズ、ビット深度、カラータイプなど取得 */
	png_get_IHDR(png_ptr, info_ptr, &wmfp->sspecs.x_size, &wmfp->sspecs.y_size, &wmfp->sspecs.bit_depth,
			&wmfp->sspecs.color_type, &interlace_method, &compression_method, &filter_method);

	if(interlace_method){	/* インターレース方式を採用した画像は対象外 */
		puts("interlace is valid, not supported");
		return;
	}

#ifdef DEBUG
	printf("x:%lu, y:%lu, bit_depth:%d, color_type:%d, interlace:%d, compression:%d, filter:%d\n"
		,wmfp->sspecs.x_size, wmfp->sspecs.y_size, wmfp->sspecs.bit_depth, wmfp->sspecs.color_type,
		interlace_method, compression_method, filter_method);
#endif

	wmfp->sspecs.row_pointers = (png_bytep *)malloc(sizeof(png_bytep) * wmfp->sspecs.y_size);

	/* メモリを確保 これで列単位でアクセスできるようになる */
	for(i = 0; i < wmfp->sspecs.y_size; i++){
		wmfp->sspecs.row_pointers[i] = (png_byte *)malloc(png_get_rowbytes(png_ptr, info_ptr));

		if(wmfp->sspecs.row_pointers[i] == NULL){
			puts("malloc err");
		}
	}

	/* ここからバイト列の読み込み */
	if (setjmp(png_jmpbuf(png_ptr))){
		puts("[readPngFile] Error during read_image");
	}

	for(i = 0; i < wmfp->sspecs.y_size; i++){
		png_read_row(png_ptr, wmfp->sspecs.row_pointers[i], NULL);
	}

	printf("offset_x:%lu, offset_y:%lu\n",  png_get_x_offset_pixels(png_ptr, info_ptr),  png_get_y_offset_pixels(png_ptr, info_ptr));
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);	/* libpng内データの解放 */

	fclose(fp);
}

/*
	PNGファイルを書き込む関数
	@wmfp 扱っているWFILEのアドレス
*/
static void writePngFile(WFILE *wmfp)
{
	int i;
	struct png_shared_specs *sspecs = &wmfp->sspecs;
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

	png_init_io(png_ptr, wmfp->out_fp);

	/* ヘッダを書き込む */
	if(setjmp(png_jmpbuf(png_ptr))){
		puts("[writePngFile] Error during writing header");
	}

	png_set_IHDR(png_ptr, info_ptr, sspecs->x_size, sspecs->y_size,
		sspecs->bit_depth, sspecs->color_type, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	/* オフセットを書き込む */
	setOffsetToChunk(&wmfp->offset, png_ptr, info_ptr);

	png_write_info(png_ptr, info_ptr);

	//png_write_image(png_ptr, sspecs->row_pointers);

	for(i = 0; i < sspecs->y_size; i++){
		png_write_row(png_ptr, sspecs->row_pointers[i]);
	}

	puts("write complete");

	/* end write */
	if(setjmp(png_jmpbuf(png_ptr))){
		puts("[writePngFile] Error during end of write");
	}

	png_write_end(png_ptr, NULL);
	png_destroy_write_struct(&png_ptr, &info_ptr);	/* libpng内データの解放 */
}


/*
	wopen()で渡されたファイル名とモードに基づいてPNGのイメージを作成する関数
	@wmfp 対象のWFILEのアドレス
*/
static void openImageByMode(WFILE *wmfp)
{
	size_t len;
	char *hidden_path;	/* 隠しファイル */

	len = strlen(wmfp->path) + 1 + 1;	/* "." + "\0" */

	hidden_path = (char *)malloc(len);

	sprintf(hidden_path, ".%s", wmfp->path);	/* 隠しファイルのパスを生成 */

#ifdef DEBUG
	printf("hidden_path:%s\n", hidden_path);
#endif

	/* ファイル一つにつきreadPngFile１回 */

	if(access(wmfp->path, F_OK) == 0){	/* ファイルが存在する場合 */

		if(wmfp->mode.split.can_read && wmfp->mode.split.can_write == 0){	/* 読み込みだけ */
			/* pathからspecを作る */
			readPngFile(wmfp->path, wmfp);
		}
		else if(wmfp->mode.split.can_read == 0 && wmfp->mode.split.can_write){	/* 書き込みだけ */
			if(wmfp->mode.split.can_trancate){	/* 長さを切り詰める場合 */
				/* もと画像からspecを作る */
				readPngFile(BASE_IMG, wmfp);
			}
			else{
				/* pathからspecを作る */
				readPngFile(wmfp->path, wmfp);
			}
			/* 書き込み用にwmfp->out_fpもオープンしておく */
			wmfp->out_fp = fopen(wmfp->path, "wb");
		}
		else if(wmfp->mode.split.can_read && wmfp->mode.split.can_write){	/* 読み込み＋書き込み */
			if(wmfp->mode.split.can_trancate){	/* 長さを切り詰める場合 */
				/* もと画像からspecを作る */
				readPngFile(BASE_IMG, wmfp);
			}
			else{
				readPngFile(wmfp->path, wmfp);
			}
			/* 書き込み用にwmfp->out_fpもオープンしておく */
			wmfp->out_fp = fopen(wmfp->path, "wb");
		}

	}
	else{	/* ファイルが存在しない場合 */
		if(wmfp->mode.split.can_create){	/* 新規作成できるなら作成する */
			/* もと画像からspecを作る */
			readPngFile(BASE_IMG, wmfp);

			wmfp->out_fp = fopen(wmfp->path, "wb");
		}
		else{
			puts("err");
		}
	}

	free(hidden_path);
	free(wmfp->path);	/* wopen()でmallocした分 */
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
	size_t len;

	if((wmfp = malloc(sizeof(WFILE))) == NULL){
		return NULL;
	}

	wmfp->r_cspecs.shared_specs = &wmfp->sspecs;
	wmfp->w_cspecs.shared_specs = &wmfp->sspecs;

	len = sizeof(char) * (strlen(path) + 1);	/* strlen(3)は終端文字分を計算しないので+1 */

	wmfp->path = (char *)malloc(len);	/* free()はopenImageByMode()が担当する */

	strncpy(wmfp->path, path, len);

	/* strをモード変数に変換 */
	if(strcmp(str, "r") == 0){
		wmfp->mode.full = MODE_READ;
	}
	else if(strcmp(str, "r+") == 0){
		wmfp->mode.full = MODE_READ_PLUS;
	}
	else if(strcmp(str, "w") == 0){
		wmfp->mode.full = MODE_WRITE;
	}
	else if(strcmp(str, "w+") == 0){
		wmfp->mode.full = MODE_WRITE_PLUS;
	}
	else if(strcmp(str, "a") == 0){
		wmfp->mode.full = MODE_APPEND;
	}
	else if(strcmp(str, "a+") == 0){
		wmfp->mode.full = MODE_APPEND_PLUS;
	}
	else{
		puts("Invalide wopen mode");
	}

	return wmfp;
}

/*
	sizeバイトのデータを読み取り、ptrに格納する
	@ptr 読み込んだデータを格納するバッファ（@sizeのサイズが無いといけない）
	@size 読み込むサイズ（バイト）
	return 実際に読み込めたデータのバイト数
*/
size_t wread(void *ptr, size_t size, WFILE *wmfp)
{
	int i;
	char result;
	char *buf = (char *)ptr;
	size_t ret = 0;

	wmfp->size = size;

	//wmfp->sspecs.row_pointer = (png_byte *)malloc(wmfp->sspecs.row_bytes);

	openImageByMode(wmfp);

	for(i = 0; i < size; i++){	/* バイトごとにループを回す */
		if((result = wread_byte(wmfp)) == -1){
#ifdef DEBUG
			printf("result:%d, %d byte read\n", result, i);
#endif
			return ret;
		}
		else{
			buf[i] = result;
			ret++;
		}
	}

	return ret;
}

/*
	prtからsizeバイトのデータをwmfpに書込む関数
	return 実際に書き込めたデータの個数
*/
size_t wwrite(const void *ptr, size_t size, WFILE *wmfp)
{
	int i;
	size_t ret = 0;

	openImageByMode(wmfp);

	for(i = 0; i < size; i++){
		wwrite_byte(*(char *)(ptr + i), wmfp);
		ret++;
	}

	return ret;
}

/*
	WFILEのエントリのメモリデータを解放する関数
	@wmfp 
*/
void wclose(WFILE *wmfp)
{
	int y;

#if 0
	if(wmfp->r_cspecs.fp){
		fclose(wmfp->r_cspecs.fp);
	}
	if(wmfp->w_cspecs.fp){
		fclose(wmfp->w_cspecs.fp);
	}

	free(wmfp->sspecs.row_pointer);
#endif

	if(wmfp->mode.split.can_write){	/* 書込み許可なモードだとout_fpがオープンされている */
		writePngFile(wmfp);

		if(wmfp->out_fp){
			fclose(wmfp->out_fp);
		}
	}

	/* sspecs内のメモリの解放 */
	for(y = 0; y < wmfp->sspecs.y_size; y++){
		free(wmfp->sspecs.row_pointers[y]);
	}

	free(wmfp->sspecs.row_pointers);
	free(wmfp);
}

