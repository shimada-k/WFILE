#include <png.h>

#define MODE_READ		0x01
#define MODE_READ_PLUS	0x03
#define MODE_WRITE		0x0E
#define MODE_WRITE_PLUS	0x0F
#define MODE_APPEND		0x16
#define MODE_APPEND_PLUS	0x17

#define BASE_IMG		"./img/logo_mini.png"	/* もと画像 */

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

struct png_operation_specs{
	int x_size, y_size;		/* 画像の縦、横のサイズ */
	int number_of_passes;	/* インターレース関連 */
	png_byte color_type;		/* カラータイプ */
	png_byte bit_depth;		/* ビット深度 */

	png_bytep *row_pointers;	/* この宣言での実体は二重ポインタ */
};

typedef struct {
	struct png_operation_specs	specs;	/* 画像管理用構造体 */

	wfile_mode_t mode;

	FILE *out_fp;				/* wcloseで書き出す際のFILEが格納される */

	woff_t	offset;			/* オフセット */
	BSTREAM *bs;				/* ここからシーケンシャルに入出力 */
}WFILE;

WFILE *wopen(const char *path, const char *str);
size_t wread(void *ptr, size_t size, WFILE *stream);
size_t wwrite(const void *ptr, size_t size, WFILE *stream);
void wclose(WFILE *stream);
