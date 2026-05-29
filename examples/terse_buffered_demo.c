/**
 * @file terse_buffered_demo.c
 * @brief terse バッファドモード + 代替スクリーンの API 検証プロトタイプ（Phase 5.5）
 *
 * terse 本体の Phase 5 で追加された以下の API を、terse-prompt の実運用に近い形で
 * 直接叩いて使い勝手を検証する独立デモ。tprompt ライブラリは介さず terse に直接リンクする。
 *
 *   - terse_options_t.render_mode = TERSE_RENDER_BUFFERED（セル仮想バッファ + 差分描画）
 *   - terse_options_t.use_alt_screen（代替スクリーン自動進入/退出）
 *   - 既存の terse_move_to / terse_set_style / terse_write_text / terse_flush を
 *     「同じ API のまま」バッファドモードで使う（Phase 5 の設計核心）
 *
 * 矢印キーで色付きの箱を動かす。毎フレーム全画面を再描画するが、flush の差分描画で
 * 変更セルだけが出力されるためちらつかない。q または Ctrl+C で終了。
 *
 * バッファドモードは端末サイズ既知が前提なので、TTY 上で実行すること。
 * （パイプ等でサイズ不明なら terse が自動的に即時モードへフォールバックする。）
 *
 * 【API 検証メモ】terse 自体は termios の raw モード遷移 API を提供していない。
 * キー入力をエコー無し・行バッファ無しで受け取るには利用者側で raw モードに入る
 * 必要がある（terse-prompt も tprompt_session.c で自前管理している）。このデモも
 * POSIX 用の最小 raw モード処理を持つ。これは Phase 5 のバグではなく terse の
 * 現状の設計境界で、Phase 5.5 の検証で再確認された点。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <terse.h>

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>

static struct termios g_saved_termios;
static int g_raw_active = 0;

static int enter_raw_mode(void)
{
	struct termios raw;
	if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) {
		return -1;
	}
	raw = g_saved_termios;
	raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= CS8;
	raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
		return -1;
	}
	g_raw_active = 1;
	return 0;
}

static void leave_raw_mode(void)
{
	if (g_raw_active) {
		tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
		g_raw_active = 0;
	}
}
#else
static int enter_raw_mode(void)
{
	return 0;
}
static void leave_raw_mode(void) { }
#endif

static void draw_frame(terse_handle_t t, int rows, int cols, int box_row, int box_col)
{
	int r;
	int c;

	/* 背景: 全セルを空白で塗る。バッファドモードなので flush まで実出力されない。
	 * 全画面を毎フレーム書いても、差分描画が変化分だけ出力する。 */
	terse_style_t bg = terse_style_default();
	terse_set_style(t, &bg);
	for (r = 0; r < rows; r++) {
		terse_move_to(t, r, 0);
		for (c = 0; c < cols; c++) {
			terse_write_text(t, " ");
		}
	}

	/* ヘッダ行（シアン太字）。 */
	terse_style_t header = terse_style_default();
	header.foreground = terse_color_basic(TERSE_BASIC_COLOR_CYAN, 1);
	header.effects = TERSE_STYLE_BOLD;
	terse_set_style(t, &header);
	terse_move_to(t, 0, 0);
	terse_write_text(t, "terse buffered-mode demo — arrows to move, q to quit");

	/* 箱（マゼンタ背景）。3x3 のブロック。 */
	terse_style_t box = terse_style_default();
	box.background = terse_color_basic(TERSE_BASIC_COLOR_MAGENTA, 0);
	box.foreground = terse_color_basic(TERSE_BASIC_COLOR_WHITE, 1);
	terse_set_style(t, &box);
	for (r = 0; r < 3; r++) {
		int br = box_row + r;
		if (br < 1 || br >= rows) {
			continue;
		}
		terse_move_to(t, br, box_col);
		for (c = 0; c < 3; c++) {
			if (box_col + c < cols) {
				terse_write_text(t, (r == 1 && c == 1) ? "@" : " ");
			}
		}
	}

	/* フッタに座標表示（緑）。 */
	terse_style_t footer = terse_style_default();
	footer.foreground = terse_color_basic(TERSE_BASIC_COLOR_GREEN, 0);
	terse_set_style(t, &footer);
	if (rows >= 2) {
		char line[64];
		snprintf(line, sizeof(line), "box at (%d, %d)   size %dx%d", box_row, box_col, rows, cols);
		terse_move_to(t, rows - 1, 0);
		terse_write_text(t, line);
	}

	/* これ 1 回で差分だけが端末へ出る。 */
	terse_flush(t);
}

int main(void)
{
	terse_options_t opts;
	memset(&opts, 0, sizeof(opts));
	opts.input_fd = 0;
	opts.output_fd = 1;
	opts.codec_name = "UTF-8";
	opts.render_mode = TERSE_RENDER_BUFFERED; /* Phase 5: バッファドモード opt-in */
	opts.use_alt_screen = 1;				  /* Phase 5: 代替スクリーン自動進入/退出 */

	terse_handle_t t = terse_open(TERSE_PROFILE_AUTO, &opts);
	if (!t) {
		fprintf(stderr, "terse_open failed\n");
		return 1;
	}

	terse_size_t sz = terse_get_size(t);
	if (!sz.known) {
		/* 非 TTY 等。バッファドモードは無効化されているのでそのまま終了。 */
		fprintf(stderr, "terminal size unknown; run this in a real terminal (TTY).\n");
		terse_close(t);
		return 1;
	}

	int rows = sz.rows;
	int cols = sz.cols;
	int box_row = rows / 2;
	int box_col = cols / 2;

	/* terse は raw モード遷移を提供しないので利用者側で入る（API 検証メモ参照）。 */
	if (enter_raw_mode() != 0) {
		fprintf(stderr, "failed to enter raw mode\n");
		terse_close(t);
		return 1;
	}

	terse_show_cursor(t, 0);
	draw_frame(t, rows, cols, box_row, box_col);

	for (;;) {
		terse_event_t ev;
		terse_error_t rc = terse_read_event(t, -1, &ev);
		if (rc != TERSE_OK) {
			continue;
		}

		int quit = 0;
		switch (ev.type) {
		case TERSE_EVENT_CHAR:
			if (ev.data.ch.scalar == 'q' || ev.data.ch.scalar == 'Q') {
				quit = 1;
			}
			break;
		case TERSE_EVENT_ARROW_UP:
			if (box_row > 1) {
				box_row--;
			}
			break;
		case TERSE_EVENT_ARROW_DOWN:
			if (box_row + 3 <= rows) {
				box_row++;
			}
			break;
		case TERSE_EVENT_ARROW_LEFT:
			if (box_col > 0) {
				box_col--;
			}
			break;
		case TERSE_EVENT_ARROW_RIGHT:
			if (box_col + 3 <= cols) {
				box_col++;
			}
			break;
		case TERSE_EVENT_RESIZE:
			rows = ev.data.resize.rows;
			cols = ev.data.resize.cols;
			if (box_row >= rows) {
				box_row = rows - 1;
			}
			if (box_col >= cols) {
				box_col = cols - 1;
			}
			break;
		default:
			break;
		}

		if (quit) {
			break;
		}
		draw_frame(t, rows, cols, box_row, box_col);
	}

	terse_show_cursor(t, 1);
	terse_close(t);	  /* use_alt_screen により代替スクリーンを自動退出 */
	leave_raw_mode(); /* alt screen を抜けてプライマリへ戻った後で termios を復元 */
	return 0;
}
