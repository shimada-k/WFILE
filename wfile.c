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
	printf("[total_bit:%d plane_no:%d (x:%d, y:%d), color:%d]\n", oft->total_bit, oft->plane_no, oft->x, oft->y, oft->color);
}
#endif


/*
	WFILEのオフセットから次に処理するべきバイトの内容を返す
	@wmfp 処理するWFILEエントリのポインタ
	return 処理するべきバイトの内容
*/
static png_byte getColorFromOft(WFILE *wmfp)
{
	return wmfp->sspecs.row_pointer[wmfp->offset.x * 4 + wmfp->offset.color];
}

/*
	WFILEのオフセットからvalの値をwmfp->imgに埋め込む
	@val 色の輝度値
	@wmfp 処理の対象のWFILEのポインタ
*/
static void setColorFromOft(png_byte val, WFILE *wmfp)
{
	wmfp->sspecs.row_pointer[wmfp->offset.x * 4 + wmfp->offset.color] = val;
}

/*	
	一行に埋め込める空かしのバイト数を計算し、返す関数
	@wmfp 対象のWFILEのアドレス
	return 1行であるビットプレーンに埋め込める透かしのバイト数
*/
static unsigned int calcWritableByte_row(WFILE *wmfp)
{
	return (wmfp->sspecs.row_bytes / 8);
}

/*
	offsetに画像の位置オフセットを格納する関数。offset->total_bitが適切に設定されている必要がある
	@offset 格納するoffset変数
*/
static void calcPixelOffset(woff_t *offset, const struct png_shared_specs *sspecs)
{
	int plane_bit_total, offset_odd;

	plane_bit_total = sspecs->x_size * sspecs->y_size * 4;	/* 1ビットプレーン何ビット入るか */
	offset_odd = offset->total_bit % plane_bit_total;

	offset->plane_no = offset->total_bit / plane_bit_total;
	offset->y = offset_odd / (sspecs->x_size * 4);
	offset->x = (offset_odd % (sspecs->x_size * 4)) / 4;	/* 1ピクセル4ドットなので最後に4で割る */
	offset->color = COLOR_RED;
}

/*
	PNGのテキストチャンクからオフセットを読み込み、offsetにセットする関数
	※画像データの読み書きの前に呼び出さないといけない
	@cspecs テキストチャンクにアクセスするために必要
	@offset セットするwoff_t
*/
static void getOffsetFromChunk(woff_t *offset, struct png_control_specs *cspecs)
{
	png_text *text;
	int num_comments, num_text;

	/* コメントの取得 */
	num_comments = png_get_text(cspecs->png_ptr, cspecs->info_ptr, &text, &num_text);

	printf("num_comments : %d\n", num_comments);

	if(num_comments == 0){
		return;
	}

#ifdef DEBUG
	printf("[key words]\n%s\n", text->key);
	printf("[text body]\n%s\n", text->text);
#endif

	offset->total_bit = atoi(text->text);

	calcPixelOffset(offset, cspecs->shared_specs);	/* plane_no, x, y, colorを計算する */
}

/*
	offsetを文字列化してPNGのテキストチャンクに埋め込む関数
	※この関数はrow_pointerに読み込む前に実行されないといけない
	@offset 埋め込むwoff_t
	@cspecs テキストチャンクにアクセスするために必要
*/
static void setOffsetToChunk(const woff_t *offset, struct png_control_specs *cspecs)
{
	png_text text;
	char buf[32];

	/* メモリの確保 */

	text.compression = PNG_TEXT_COMPRESSION_NONE;
	text.key = (char *)malloc(sizeof(char) * 16);
	text.text = (char *)malloc(sizeof(char) * 32);

	/* オフセットは書き込んであるビット数だけ */
	sprintf(buf, "%d", offset->total_bit);

	strcpy(text.key, "WaterMark offset");
	strcpy(text.text, buf);

	png_set_text(cspecs->png_ptr, cspecs->info_ptr, (png_textp)&text, 1);

#ifdef DEBUG
	puts("The offset wrote");
	printOffset(offset);
#endif

	png_write_info(cspecs->png_ptr, cspecs->info_ptr);

	/* メモリを解放する */
	free(text.key);
	free(text.text);
}

/*
	wmfp->offsetを1ドット分だけ進める関数
	return 成功:0 失敗:-1
*/
static int wseek_dot(WFILE *wmfp)
{
	woff_t *offset;

	offset = &wmfp->offset;

	/* oftを更新（座標、色、ビットプレーンの番号） */
	if(offset->color == COLOR_ALPHA){	/* RGBAの最後のカラーだったら */
		if(offset->x == wmfp->sspecs.x_size - 1){	/* ビットプレーンの横軸のMAXまでいっていたら */
			if(offset->y == wmfp->sspecs.y_size -1){	/* ビットプレーンの最後までいっていたら */
				offset->y = 0;
				offset->x = 0;

				if(offset->plane_no == 7){
#ifdef DEBUG
					printOffset(offset);
					puts("wseek_dot:watermark size over-flowed");
#endif
					return -1;	/* 画像に埋め込める上限を越えたときは考慮されていない */
				}
				else{
					offset->plane_no++;
				}
			}
			else{
				offset->x = 0;
				offset->y++;
			}
		}
		else{
			offset->x++;
		}
		offset->color = COLOR_RED;
	}
	else{	/* ピクセル内で色を変えるだけ(ピクセル位置は変えない) */
		offset->color++;
	}

	offset->total_bit++;

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

	for(i = 0; i < 8; i++){			/* 8ドット分ループ */
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
static void wwrite_byte(const char val, WFILE *wmfp)
{
	int i, bit;
	png_byte color;

	wmfp->bs = openBitStream(&val, sizeof(char), "r");	/* valをもとにビットストリームを作成 */

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
		printOffset(&wmfp->offset);
#endif
		setColorFromOft(color, wmfp);	/* ここでrow_pointersに格納 */

		/* 1ドット分だけオフセットを進める */
		wseek_dot(wmfp);
	}

	closeBitStream(wmfp->bs);
}


#define SIG_CHECK_LEN	8	/* PNGシグネチャをチェックするバイト数（4-8） */

/*
	読み込み用のpng_control_specsを設定する関数
	@fname ファイル名
	@r_specs 設定するpng_control_specsのアドレス
*/
static void createReadCspecs(const char *fname, struct png_control_specs *cspecs)
{
	char header[SIG_CHECK_LEN];    // 8 is the maximum size that can be checked
	int interlace_method, compression_method, filter_method;
	/* open file and test for it being a png */

	cspecs->fp = fopen(fname, "rb");

	if(cspecs->fp == NULL){
		printf("[readPngFile] File %s could not be opened for reading\n", fname);
	}

	fread(header, 1, SIG_CHECK_LEN, cspecs->fp);

	if(png_sig_cmp((png_bytep)header, 0, SIG_CHECK_LEN)){
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
	png_set_sig_bytes(cspecs->png_ptr, SIG_CHECK_LEN);

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
	pathの隠しファイルのパスの文字列をhidden_pathに生成する関数
	※hidden_pathはどこかでfree(3)する必要がある
	@path もとになるパス名
	return 隠しファイルのパスを記述したchar配列のポインタ
*/
static char *allocHiddenPath(const char *path)
{
	size_t len;
	char *hidden_path;

	len = strlen(path) + 1 + 1;	/* "." + "\0" */

	hidden_path = (char *)malloc(len);

	sprintf(hidden_path, "_%s", path);	/* 隠しファイルのパスを生成 */

#ifdef DEBUG
	printf("hidden_path:%s\n", hidden_path);
#endif

	return hidden_path;
}

/*
	wopen()で渡されたファイル名とモードに基づいてPNGのイメージを作成する関数
	@wmfp 対象のWFILEのアドレス
*/
static void openImageByMode(WFILE *wmfp)
{
	char *hidden_path = allocHiddenPath(wmfp->path);	/* 隠しファイル */

	/* ファイル一つにつきreadPngFile１回 */

	if(access(wmfp->path, F_OK) == 0){	/* ファイルが存在する場合 */

		if(wmfp->mode.split.can_write){	/* 書き込みできたら */
			if(wmfp->mode.split.can_trancate){	/* 長さを切り詰める場合 */
				/* もと画像からspecを作る */
				createReadCspecs(BASE_IMG, &wmfp->r_cspecs);
			}
			else{
				/* pathからspecを作る */
				createReadCspecs(wmfp->path, &wmfp->r_cspecs);
			}
			/* 隠しファイルをオープン */
			createWriteCspecs(hidden_path, &wmfp->w_cspecs);
		}
		else{
			/* pathからspecを作る */
			createReadCspecs(wmfp->path, &wmfp->r_cspecs);
		}
	}
	else{	/* ファイルが存在しない場合 */
		if(wmfp->mode.split.can_create){	/* 新規作成できるなら作成する */
			/* もと画像からspecを作る */
			createReadCspecs(BASE_IMG, &wmfp->r_cspecs);

			createWriteCspecs(hidden_path, &wmfp->w_cspecs);
		}
		else{
			puts("err");
		}
	}

	free(hidden_path);
}


/*
	png_write_row()は１行単位でしか処理を行えず、seekするAPIも用意されてないので、
	１枚分書き込んだら、再度openしなおさないといけない
	@wmfp WFILEエントリ
*/
static void reOpenImage(WFILE *wmfp)
{
	if(wmfp->r_cspecs.fp){
		png_destroy_read_struct(&wmfp->r_cspecs.png_ptr, &wmfp->w_cspecs.info_ptr, NULL);
		fclose(wmfp->r_cspecs.fp);
	}

	if(wmfp->w_cspecs.fp){
		char *hidden_path;

		png_write_end(wmfp->w_cspecs.png_ptr, NULL);

		png_destroy_write_struct(&wmfp->w_cspecs.png_ptr, &wmfp->w_cspecs.info_ptr);
		fclose(wmfp->w_cspecs.fp);

		hidden_path = allocHiddenPath(wmfp->path);

		rename(hidden_path, wmfp->path);	/* mv ./.img.png img.png */
		free(hidden_path);
	}

	/* png_ptr, info_ptrを作り直す */
	openImageByMode(wmfp);

	/* wmfp->offsetの設定はwwrite()の中のsetOffset()で済んでいる */
}

/*
	１行分のデータをwmfp->sspecs.row_pointersから読み込む関数
	@wmfp 対象のWFILEのアドレス
	@ptr 読み込んだ結果を格納するバッファ
	@size ptrへ格納するバイトサイズ
	return 読み込んだバイト数
*/
static size_t wread_row(WFILE *wmfp, char *ptr, size_t size)
{
	int i;
	size_t ret = 0;

#ifdef DEBUG
	if(size < wmfp->sspecs.row_bytes){
		printf("[wread_row]Notice, End line size = %lu\n", size);	/* size < row_bytesだった時 */
	}
#endif

	if (setjmp(png_jmpbuf(wmfp->r_cspecs.png_ptr))){
		puts("[wread_row] Error during read_image");
	}

	png_read_row(wmfp->r_cspecs.png_ptr, wmfp->sspecs.row_pointer, NULL);	/* 画像から1行分のデータをrow_pointerに読み込む */

	for(i = 0; i < size; i++){
		if((ptr[i] = wread_byte(wmfp)) == -1){
			puts("wread_row err");
		}
		else{
			ret++;
		}
	}

	return ret;
}

/*
	１行分のデータをwmfp->sspecs.row_pointersに書き込む関数
	※この関数が呼び出される前に、次に書き込まれる行まで頭出しされていること前提
	@wmfp 対象のWFILEのアドレス
	@ptr 1行分のバイトデータ
	@size ptrからフェッチするサイズ
	return 書き込んだバイト数
*/
static size_t wwrite_row(WFILE *wmfp, const char *ptr, size_t size)
{
	int i;
	size_t ret = 0;


#ifdef DEBUG
	if(size < calcWritableByte_row(wmfp)){
		printf("[wwrite_row]Notice, End line size = %lu\n", size);	/* size < row_bytesだった時 */
	}
#endif

	memset(wmfp->sspecs.row_pointer, 0, wmfp->sspecs.row_bytes);

	//if(setjmp(png_jmpbuf(wmfp->r_cspecs.png_ptr))){
	//	puts("[write_row] Error during read_image");
	//}

	png_read_row(wmfp->r_cspecs.png_ptr, wmfp->sspecs.row_pointer, NULL);	/* 画像から1行分のデータをrow_pointerに読み込む */

	if(ptr == NULL){	/* NULLだったら何も書き込まない */
		;
	}
	else{
		for(i = 0; i < size; i++){		/* size分のデータに透かしを埋め込む */
			wwrite_byte(ptr[i], wmfp);
			ret++;
		}
	}

	png_write_row(wmfp->w_cspecs.png_ptr, wmfp->sspecs.row_pointer);	/* データをrow_pointerに書き込む */

	/* 一枚分書き込んだら、もう一度オープンしなおす */
	if(wmfp->offset.y == 0 && wmfp->offset.x == 0 && wmfp->offset.color == COLOR_RED){
		reOpenImage(wmfp);
	}

	return ret;
}


/* データを単位データ量で扱う時のデータ構造 */
typedef struct {
	int first_piece;
	int num_even;
	int end_piece;
}DDECOMP_T;

/*
	sizeバイトの領域を行単位でfirst_piece, num_even, end_pieceに分割する関数
	@wmfp 対象のWFILEのアドレス
	@first_piece, @num_even, @end_piece 格納される変数のアドレス（単位：バイト）
	@size データのサイズ
*/
static void decompWaterMark_row(WFILE *wmfp, DDECOMP_T *decomp, size_t size)
{
	int row_writable_bytes;

	row_writable_bytes = calcWritableByte_row(wmfp);

	decomp->first_piece = (wmfp->sspecs.x_size - wmfp->offset.x) * 4 / 8;

	if(size < decomp->first_piece){	/* sizeが極端に小さい値だったら */
		decomp->first_piece = size;
	}
	else{
		;
	}

	decomp->num_even = (size - decomp->first_piece) / row_writable_bytes;
	decomp->end_piece = (size - decomp->first_piece) % row_writable_bytes;

#ifdef DEBUG
	printf("first_piece:%d\n", decomp->first_piece);
	printf("num_even:%d\n", decomp->num_even);
	printf("end_piece:%d\n", decomp->end_piece);
#endif
}

/*
	pathの画像の最後のチャンクから、オフセットを読み取り、offsetに設定する関数
	@path オフセットが格納されているPNGファイルのパス
	@offset 格納先のwoff_t変数のアドレス
*/
static void getOffsetFromChunk_2ndHarf(const char *path, woff_t *offset)
{
	struct png_control_specs cspecs;
	struct png_shared_specs sspecs;
	int i;
	png_byte *row_ptr;

	cspecs.shared_specs = &sspecs;	/* アドレスを設定 */

	createReadCspecs(path, &cspecs);	/* この中でsspecsも設定される */

	row_ptr = (png_byte *)malloc(sspecs.row_bytes);

	for(i = 0; i < sspecs.y_size; i++){	/* 一回最後まで読まないとだめなので、最後まで読む */
		png_read_row(cspecs.png_ptr, row_ptr, NULL);
	}

	free(row_ptr);

	png_read_end(cspecs.png_ptr, cspecs.info_ptr);

	getOffsetFromChunk(offset, &cspecs);

#ifdef DEBUG
	printOffset(offset);
#endif

	png_destroy_read_struct(&cspecs.png_ptr, &cspecs.info_ptr, NULL);

	fclose(cspecs.fp);
}

/******************************
*
*	公開用ライブラリ関数
*
*******************************/

/*
	WFILEのエントリを作成する関数
	@path ファイルのパス文字列
	@str モード文字列
	return メモリを確保したWFILEのアドレス
*/
WFILE *wopen(const char *path, const char *str)
{
	WFILE *wmfp = NULL;
	size_t len;

	if((wmfp = malloc(sizeof(WFILE))) == NULL){
		return NULL;
	}

	/* アドレスをメモする */
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

	if(wmfp->mode.split.write_pos_end){
	}
	else{
	}

	if(wmfp->mode.split.write_pos_end){	/* 追記モードだったら画像からオフセットを設定 */
		getOffsetFromChunk_2ndHarf(path, &wmfp->offset);
	}
	else{	/* 追記モードでなかったら、オフセットは先頭から(0で埋める) */
		memset(&wmfp->offset, 0, sizeof(woff_t));
	}

	openImageByMode(wmfp);	/* r_cspecsとw_cspecsを準備する */

	/* row_pointerのメモリ確保 */
	wmfp->sspecs.row_pointer = (png_byte *)malloc(wmfp->sspecs.row_bytes);

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
	int i, row_even, row_odd, row_writable_bytes;
	char *buf = (char *)ptr;

#ifdef DEBUG
	woff_t offset;
	getOffsetFromChunk(&offset, &wmfp->r_cspecs);
	printOffset(&offset);
#endif

	row_writable_bytes = calcWritableByte_row(wmfp);
	row_even = size / row_writable_bytes;
	row_odd = size % row_writable_bytes;

#ifdef DEBUG
	printf("even:%d, odd:%d\n", row_even, row_odd);
#endif

	for(i = 0; i < row_even; i++){
		buf += wread_row(wmfp, buf, row_writable_bytes);
#ifdef DEBUG
		printf("%lu byte read\n", (char *)buf - (char *)ptr);
#endif
	}

	buf += wread_row(wmfp, buf, row_odd);
#ifdef DEBUG
	printf("%lu byte read\n", (char *)buf - (char *)ptr);
#endif

	return ((char *)buf - (char *)ptr);
}

/*
	prtからsizeバイトのデータをwmfpに書込む関数
	return 実際に書き込めたデータの個数
*/
size_t wwrite(const void *ptr, size_t size, WFILE *wmfp)
{
	char *buf = (char *)ptr;
	int i;
	DDECOMP_T decomp_row;

	/* 追記モードだったら、offsetに従ってseekして頭出し */
	if(wmfp->mode.split.write_pos_end){
		for(i = 0; i < wmfp->offset.y; i++){
			wwrite_row(wmfp, NULL, 0);
		}
	}

	/* first_piece, num_even, end_pieceの計算 */
	decompWaterMark_row(wmfp, &decomp_row, size);

	/* 実際に行ごとに書き込む */
	if(decomp_row.first_piece){
		buf += wwrite_row(wmfp, buf, decomp_row.first_piece);
	}

	for(i = 0; i < decomp_row.num_even; i++){
		buf += wwrite_row(wmfp, buf, calcWritableByte_row(wmfp));
	}

	if(decomp_row.end_piece){
		buf += wwrite_row(wmfp, buf, decomp_row.end_piece);
	}

#ifdef DEBUG
	printf("%lu byte wrote\n", buf - (char *)ptr);
#endif

	return (buf - (char *)ptr);
}

/*
	WFILEのエントリのメモリデータを解放する関数
	@wmfp 対象のWMFILEのアドレス
*/
void wclose(WFILE *wmfp)
{
	int i;

	if(wmfp->w_cspecs.fp){
		char *hidden_path;

		/* 最後まで書き出す */
		for(i = (wmfp->offset.x == 0 ? wmfp->offset.y : wmfp->offset.y + 1); i < wmfp->sspecs.y_size; i++){
			wwrite_row(wmfp, NULL, 0);
		}

		/* offsetを画像に書き込む */
		setOffsetToChunk(&wmfp->offset, &wmfp->w_cspecs);
		png_write_end(wmfp->w_cspecs.png_ptr, wmfp->w_cspecs.info_ptr);

		/* ライブラリ内リソース解放 */
		png_destroy_write_struct(&wmfp->w_cspecs.png_ptr, &wmfp->w_cspecs.info_ptr);
		fclose(wmfp->w_cspecs.fp);

		/* 隠しファイルを置き換える */
		hidden_path = allocHiddenPath(wmfp->path);
		rename(hidden_path, wmfp->path);

		free(hidden_path);
	}

	if(wmfp->r_cspecs.fp){
		png_destroy_read_struct(&wmfp->r_cspecs.png_ptr, &wmfp->r_cspecs.info_ptr, NULL);
		fclose(wmfp->r_cspecs.fp);
	}

	/* メモリ解放 */
	free(wmfp->sspecs.row_pointer);
	free(wmfp->path);	/* wopen()でmallocした分 */
	free(wmfp);
}

