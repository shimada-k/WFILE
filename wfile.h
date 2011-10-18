#include <png.h>

#define MODE_READ		0x01
#define MODE_READ_PLUS	0x03
#define MODE_WRITE		0x0E
#define MODE_WRITE_PLUS	0x0F
#define MODE_APPEND		0x16
#define MODE_APPEND_PLUS	0x17

#define BASE_IMG		"./img/logo_mini.png"	/* デフォルトもと画像 */

#define STR_PATH_MAX		128	/* wopen()のpathが指す最大バイト数 */

typedef union wfile_mode{	/* wopen()のモードを表す構造体 */
	struct{
		unsigned char can_read:1;
		unsigned char can_write:1;

		unsigned char can_create:1;
		unsigned char can_trancate:1;
		unsigned char write_pos_end:1;
	}split;
	unsigned char full;
}wfile_mode_t;

typedef struct {	/* 次に画像中のどのピクセルを処理するか（カレントオフセット） */
	int plane_no;
	int x, y, color;	/* ピクセルの座標＋色（RED:0, GREEN:1, BLUE:2） */
}woff_t;

/* 作業用のPNGデータ構造 */
struct png_shared_specs{
	png_uint_32 x_size, y_size;	/* 画像の縦、横のサイズ */
	png_uint_32 row_bytes;	/* 1行のバイト数 */
	int color_type;		/* カラータイプ */
	int bit_depth;		/* ビット深度 */

	png_bytep row_pointer;
	png_bytep *row_pointers;
};

/* 管理用のPNGデータ構造 */
struct png_control_specs{
	FILE *fp;

	png_structp png_ptr;		/* PNG管理用構造体 */
	png_infop info_ptr;		/* PNG管理用構造体 */

	struct png_shared_specs *shared_specs;	/* 共通データ */
};

typedef struct {
	char *path;	/* ファイル名 */
	size_t size;	/* wopenで渡されるsize */

	struct png_shared_specs sspecs;	/* 画像管理用構造体 */
	struct png_control_specs r_cspecs, w_cspecs;	/* 読み込み用と書き込み用のcspecs */

	wfile_mode_t mode;

	FILE *out_fp;				/* wcloseで書き出す際のFILEが格納される */

	woff_t	offset;			/* オフセット */
	BSTREAM *bs;				/* ここからシーケンシャルに入出力 */
}WFILE;

WFILE *wopen(const char *path, const char *str);
size_t wread(void *ptr, size_t size, WFILE *stream);
size_t wwrite(const void *ptr, size_t size, WFILE *stream);
void wclose(WFILE *stream);
