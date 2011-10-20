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

#ifdef DEBUG
	puts("The offset wrote");
	printOffset(offset);
#endif

	/* メモリを解放する */
	free(text_ptr);
	free(text_ptr->key);
	free(text_ptr->text);
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
	if(size < wmfp->sspecs.row_bytes / 8){
		printf("[wwrite_row]Notice, End line size = %lu\n", size);	/* size < row_bytesだった時 */
	}
#endif

	memset(wmfp->sspecs.row_pointer, 0, wmfp->sspecs.row_bytes);

	if (setjmp(png_jmpbuf(wmfp->r_cspecs.png_ptr))){
		puts("[write_row] Error during read_image");
	}

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

	return ret;
}


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
			createReadCspecs(wmfp->path, &wmfp->r_cspecs);
		}
		else if(wmfp->mode.split.can_read == 0 && wmfp->mode.split.can_write){	/* 書き込みだけ */
			if(wmfp->mode.split.can_trancate){	/* 長さを切り詰める場合 */
				/* もと画像からspecを作る */
				createReadCspecs(BASE_IMG, &wmfp->r_cspecs);
			}
			else{
				/* pathからspecを作る */
				createReadCspecs(wmfp->path, &wmfp->r_cspecs);
			}
			/* 書き込み用にwmfp->out_fpもオープンしておく */
			createWriteCspecs(hidden_path, &wmfp->w_cspecs);
		}
		else if(wmfp->mode.split.can_read && wmfp->mode.split.can_write){	/* 読み込み＋書き込み */
			if(wmfp->mode.split.can_trancate){	/* 長さを切り詰める場合 */
				/* もと画像からspecを作る */
				createReadCspecs(BASE_IMG, &wmfp->r_cspecs);
			}
			else{
				createReadCspecs(wmfp->path, &wmfp->r_cspecs);
			}
			/* 書き込み用にwmfp->out_fpもオープンしておく */
			createWriteCspecs(hidden_path, &wmfp->w_cspecs);
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

	openImageByMode(wmfp);

	/* row_pointerのメモリ確保 */
	wmfp->sspecs.row_pointer = (png_byte *)malloc(wmfp->sspecs.row_bytes);

	row_writable_bytes = wmfp->sspecs.row_bytes / 8;
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
	sizeから最終的に画像のどの部分まで透かしが入るか計算してoffsetに格納する関数
	@wmfp 対象のWFILEのアドレス
	@offset 格納先のwoff_t変数
	@size wread()が渡されたsize
*/
static void calcOffset(const WFILE *wmfp, woff_t *new_offset, woff_t *old_offset, size_t size)
{
	//int row_writable_bytes;
	int plane_bit_total, old_bit_offset, new_bit_offset;
	int new_offset_odd;

	plane_bit_total = wmfp->sspecs.x_size * wmfp->sspecs.y_size * 4;	/* 1ビットプレーン何ビット入るか */
	old_bit_offset = plane_bit_total * old_offset->plane_no + (old_offset->y * wmfp->sspecs.x_size * 4) + old_offset->x * 4;

	new_bit_offset = old_bit_offset + size * 8;

#ifdef DEBUG
	printOffset(old_offset);
	printf("plane_bit_total:%d\n", plane_bit_total);
	printf("old_bit_offset:%d\n", old_bit_offset);
	printf("new_bit_offset:%d\n", new_bit_offset);
#endif

	//row_writable_bytes = wmfp->sspecs.row_bytes / 8;				/* 画像横１行に透かしが何バイト入るか */

	//new_offset->x = size % row_writable_bytes * 2;
	//new_offset->y = size / row_writable_bytes;
	new_offset->plane_no = new_bit_offset / plane_bit_total;

	new_offset_odd = new_bit_offset % plane_bit_total;

	new_offset->color = 0;	/* ALPHAチャンネルありの場合4ドットなので */
	new_offset->y = new_offset_odd / (wmfp->sspecs.x_size * 4);
	new_offset->x = (new_offset_odd % (wmfp->sspecs.x_size * 4)) / 4;
}

/*
	prtからsizeバイトのデータをwmfpに書込む関数
	return 実際に書き込めたデータの個数
*/
size_t wwrite(const void *ptr, size_t size, WFILE *wmfp)
{
	int i, row_even, row_odd, row_writable_bytes;
	woff_t new_offset, old_offset;
	char *buf = (char *)ptr;

	memset(&new_offset, 0, sizeof(woff_t));
	memset(&old_offset, 0, sizeof(woff_t));

	/*	row_even:	1行あたりに埋め込める透かしのバイト数
		row_odd:	1行では埋め込めない透かしのバイト数	*/

	openImageByMode(wmfp);

	/* offsetの設定 */
	if(wmfp->mode.split.write_pos_end){	/* 追記モードだったら画像からオフセットを設定 */
		getOffsetFromChunk(&old_offset, wmfp->r_cspecs.png_ptr, wmfp->r_cspecs.info_ptr);
	}
	else{
		wmfp->offset.plane_no = 0;
		wmfp->offset.x = 0;
		wmfp->offset.y = 0;
		wmfp->offset.color = 0;
	}

	/* row_pointerのメモリ確保 */
	wmfp->sspecs.row_pointer = (png_byte *)malloc(wmfp->sspecs.row_bytes);

	row_writable_bytes = wmfp->sspecs.row_bytes / 8;
	row_even = size / row_writable_bytes;	
	row_odd = size % row_writable_bytes;

	calcOffset(wmfp, &new_offset, &old_offset, size);

	setOffsetToChunk(&new_offset, wmfp->w_cspecs.png_ptr, wmfp->w_cspecs.info_ptr);	/* オフセットをセットする */
	png_write_info(wmfp->w_cspecs.png_ptr, wmfp->w_cspecs.info_ptr);

#ifdef DEBUG
	printf("even:%d, odd:%d\n", row_even, row_odd);
#endif

	for(i = 0; i < row_even; i++){
		buf += wwrite_row(wmfp, buf, row_writable_bytes);
#ifdef DEBUG
		printf("%lu byte wrote\n", (char *)buf - (char *)ptr);
#endif
	}
	buf += wwrite_row(wmfp, buf, row_odd);

#ifdef DEBUG
	printf("%lu byte wrote\n", (char *)buf - (char *)ptr);
#endif

	/* 最後まで書き出す */
	i += 1;

	for(; i < wmfp->sspecs.y_size; i++){
		wwrite_row(wmfp, NULL, 0);
	}

#ifdef DEBUG
	printf("%lu byte wrote\n", (char *)buf - (char *)ptr);
#endif

	return ((char *)buf - (char *)ptr);
}

/*
	WFILEのエントリのメモリデータを解放する関数
	@wmfp 対象のWMFILEのアドレス
*/
void wclose(WFILE *wmfp)
{
	char *hidden_path;
	size_t len;

	if(wmfp->r_cspecs.fp){
		png_destroy_read_struct(&wmfp->r_cspecs.png_ptr, &wmfp->w_cspecs.info_ptr, NULL);
		fclose(wmfp->r_cspecs.fp);
	}

	if(wmfp->w_cspecs.fp){

		png_write_end(wmfp->w_cspecs.png_ptr, NULL);

		png_destroy_write_struct(&wmfp->w_cspecs.png_ptr, &wmfp->w_cspecs.info_ptr);
		fclose(wmfp->w_cspecs.fp);

		len = strlen(wmfp->path) + 1 + 1;
		hidden_path = (char *)malloc(len);

		sprintf(hidden_path, ".%s", wmfp->path);
		rename(hidden_path, wmfp->path);	/* 隠しファイルを置き換える */
		free(hidden_path);
	}

	free(wmfp->path);	/* wopen()でmallocした分 */
	free(wmfp->sspecs.row_pointer);
	free(wmfp);
}

