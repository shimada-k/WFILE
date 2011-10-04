#define MODE_READ	0
#define MODE_WRITE	1

typedef struct {	/* 次に画像中のどのピクセルを処理するか（カレントオフセット） */
	int plane_no;
	int x, y, color;	/* ピクセルの座標＋色（RED:0, GREEN:1, BLUE:2） */
}woff_t;

typedef struct {
	gdImagePtr img;	/* ここにファイルの内容が入る */
	FILE *fp;		/* writeモードでopenした場合、ここに書き出し用のFILEが格納される */
	int mode;		/* MODE_* */
	int x_size, y_size;	/* 画像の縦、横のサイズ */

	woff_t	offset;	/* カレントオフセット */
	BSTREAM *bs;		/* ここからシーケンシャルに入出力 */
}WFILE;

WFILE *wopen(const char *path, const char *mode);
size_t wread(void *ptr, size_t size, WFILE *stream);
size_t wwrite(const void *ptr, size_t size, WFILE *stream);
void wclose(WFILE *stream);
