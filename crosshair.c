/*
 * crosshair.c - Overlay simples de mira para Linux/X11.
 *
 * A ideia do programa e pequena:
 *   1. conectar no servidor grafico X11;
 *   2. descobrir onde fica o primeiro monitor;
 *   3. criar uma janela sem borda, transparente e sempre por cima;
 *   4. desenhar uma mira no centro dessa janela;
 *   5. fechar quando o usuario clicar no botao vermelho.
 *
 * Organizacao do codigo:
 *   - "configuracao" guarda numeros fixos da interface;
 *   - "geometria" descreve monitor e pontos de tela;
 *   - "adaptador X11" concentra chamadas especificas do Xlib;
 *   - "aplicacao" coordena inicializacao, loop e limpeza.
 *
 * Em C nao existe classe/interface como em linguagens orientadas a objeto,
 * mas os principios de SOLID ainda ajudam:
 *   - SRP: cada funcao tem um motivo claro para mudar;
 *   - OCP: desenho, eventos e inicializacao ficam separados;
 *   - DIP: o loop principal depende de pequenas funcoes, nao de blocos
 *          enormes de X11 espalhados pelo arquivo.
 *
 * Build:
 *   make
 *
 * Execucao:
 *   /tmp/crosshair-build/crosshair
 */

#define _POSIX_C_SOURCE 200809L

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/*
 * Constantes da interface.
 *
 * Mantemos estes valores juntos para facilitar ajustes visuais sem precisar
 * procurar numeros soltos pelo arquivo. Isso tambem evita "magic numbers",
 * um dos vicios mais comuns em codigo C pequeno.
 */
#define WINDOW_WIDTH		160
#define WINDOW_HEIGHT		230
#define CROSSHAIR_X		(WINDOW_WIDTH / 2)
#define CROSSHAIR_Y		60
#define CLOSE_BUTTON_SIZE	22
#define CLOSE_BUTTON_MARGIN	8
#define CLOSE_BUTTON_TOP	200
#define FRAME_DELAY_NS		16000000L

/*
 * Geometria de um monitor.
 *
 * x/y indicam onde o monitor comeca dentro da area virtual do desktop. Isso
 * importa quando ha mais de um monitor, porque o primeiro monitor pode nao
 * comecar exatamente em (0, 0).
 */
struct monitor_geometry {
	int x;
	int y;
	int width;
	int height;
};

/* Ponto simples usado para retornar posicoes calculadas. */
struct point {
	int x;
	int y;
};

/*
 * Cores usadas no desenho.
 *
 * O X11 trabalha com pixels ja resolvidos para o colormap/visual da janela.
 * Por isso guardamos unsigned long em vez de strings como "#00ff00".
 */
struct paint_colors {
	unsigned long green;
	unsigned long red;
	unsigned long white;
};

/*
 * Estado vivo da aplicacao.
 *
 * Centralizar esses recursos em uma struct deixa claro o que precisa ser
 * criado na inicializacao e destruido no encerramento.
 */
struct app {
	Display *display;
	int screen;
	Visual *visual;
	Colormap colormap;
	Window window;
	GC gc;
	struct monitor_geometry monitor;
	struct paint_colors colors;
	bool running;
};

/*
 * Converte componentes ARGB em um pixel de 32 bits.
 *
 * Quando encontramos um visual TrueColor de 32 bits, o compositor do desktop
 * consegue interpretar o canal alpha e manter o fundo da janela transparente.
 */
static unsigned long argb_pixel(uint32_t alpha, uint32_t red,
				uint32_t green, uint32_t blue)
{
	return ((alpha & 0xff) << 24) | ((red & 0xff) << 16) |
	       ((green & 0xff) << 8) | (blue & 0xff);
}

/*
 * Espera aproximadamente um frame.
 *
 * Usamos nanosleep porque faz parte do POSIX moderno. O antigo usleep costuma
 * gerar avisos com compilacao C11 estrita.
 */
static void sleep_one_frame(void)
{
	struct timespec delay = {
		.tv_sec = 0,
		.tv_nsec = FRAME_DELAY_NS,
	};

	nanosleep(&delay, NULL);
}

/*
 * Pede ao X11 uma cor pelo formato hexadecimal.
 *
 * Se a alocacao falhar, retornamos uma cor de fallback. O programa deve
 * continuar funcionando mesmo que a cor exata nao esteja disponivel.
 */
static unsigned long allocate_color(Display *display, int screen,
				    const char *hex,
				    unsigned long fallback)
{
	Colormap colormap = DefaultColormap(display, screen);
	XColor color;

	if (!XParseColor(display, colormap, hex, &color))
		return fallback;

	if (!XAllocColor(display, colormap, &color))
		return fallback;

	return color.pixel;
}

/*
 * Define a geometria padrao a partir do proprio X11.
 *
 * Essa e a fonte de dados mais simples e sempre disponivel quando a conexao
 * X11 foi aberta com sucesso.
 */
static struct monitor_geometry default_monitor_geometry(Display *display,
							int screen)
{
	struct monitor_geometry monitor = {
		.x = 0,
		.y = 0,
		.width = DisplayWidth(display, screen),
		.height = DisplayHeight(display, screen),
	};

	return monitor;
}

/*
 * Tenta ler a primeira linha de monitor do comando xrandr.
 *
 * O formato comum e algo como:
 *   0: +*HDMI-1 1920/530x1080/300+0+0 HDMI-1
 *
 * Como xrandr e uma ferramenta externa, essa funcao e defensiva: se o comando
 * nao existir ou vier em formato inesperado, ela apenas informa falha.
 */
static bool read_first_xrandr_monitor(struct monitor_geometry *monitor)
{
	FILE *xrandr;
	char line[256];
	int width;
	int height;
	int x;
	int y;
	bool found = false;

	xrandr = popen("xrandr --listmonitors 2>/dev/null", "r");
	if (!xrandr)
		return false;

	/* Descarta o cabecalho: "Monitors: N". */
	if (!fgets(line, sizeof(line), xrandr))
		goto out;

	if (!fgets(line, sizeof(line), xrandr))
		goto out;

	if (sscanf(line, " %*d: %*s %d/%*dx%d/%*d+%d+%d",
		   &width, &height, &x, &y) == 4 ||
	    sscanf(line, " %*d: %*s %dx%d+%d+%d",
		   &width, &height, &x, &y) == 4) {
		monitor->x = x;
		monitor->y = y;
		monitor->width = width;
		monitor->height = height;
		found = true;
	}

out:
	pclose(xrandr);
	return found;
}

/*
 * Descobre o monitor usado para posicionar a mira.
 *
 * Primeiro usamos a geometria basica do X11. Depois tentamos melhorar esse
 * resultado com xrandr, que entende melhor setups com varios monitores.
 */
static struct monitor_geometry detect_first_monitor(Display *display,
						    int screen)
{
	struct monitor_geometry monitor;

	monitor = default_monitor_geometry(display, screen);
	read_first_xrandr_monitor(&monitor);

	return monitor;
}

/*
 * Procura um visual ARGB de 32 bits.
 *
 * Visual, no X11, e a descricao de como pixels sao interpretados. Para uma
 * janela transparente precisamos de um visual TrueColor com profundidade 32.
 */
static Visual *find_argb_visual(Display *display, int screen)
{
	XVisualInfo template = {
		.screen = screen,
		.depth = 32,
		.class = TrueColor,
	};
	XVisualInfo *visuals;
	Visual *visual = NULL;
	int count = 0;

	visuals = XGetVisualInfo(display,
				 VisualScreenMask | VisualDepthMask |
					 VisualClassMask,
				 &template, &count);
	if (visuals && count > 0)
		visual = visuals[0].visual;

	if (visuals)
		XFree(visuals);

	return visual;
}

/*
 * Calcula onde a janela deve nascer.
 *
 * A janela e maior que a mira porque tambem contem o botao de fechar. Por isso
 * alinhamos CROSSHAIR_X/Y ao centro real do monitor, nao o centro da janela.
 */
static struct point overlay_position(const struct monitor_geometry *monitor)
{
	struct point position = {
		.x = monitor->x + monitor->width / 2 - CROSSHAIR_X,
		.y = monitor->y + monitor->height / 2 - CROSSHAIR_Y,
	};

	return position;
}

/* Posicao do canto superior esquerdo do botao de fechar dentro da janela. */
static struct point close_button_position(void)
{
	struct point position = {
		.x = WINDOW_WIDTH - CLOSE_BUTTON_MARGIN - CLOSE_BUTTON_SIZE,
		.y = CLOSE_BUTTON_TOP,
	};

	return position;
}

/* Retorna true quando um clique caiu dentro do retangulo do botao de fechar. */
static bool is_close_button_hit(int x, int y)
{
	struct point button = close_button_position();

	return x >= button.x &&
	       x < button.x + CLOSE_BUTTON_SIZE &&
	       y >= button.y &&
	       y < button.y + CLOSE_BUTTON_SIZE;
}

/*
 * Prepara as cores do desenho.
 *
 * Com visual ARGB podemos montar pixels diretamente. Sem ele, usamos o colormap
 * padrao do X11 e aceitamos um visual sem transparencia como fallback.
 */
static struct paint_colors prepare_paint_colors(Display *display, int screen,
						Visual *visual)
{
	struct paint_colors colors;

	if (visual) {
		colors.green = argb_pixel(0xff, 0x00, 0xff, 0x00);
		colors.red = argb_pixel(0xff, 0xcc, 0x22, 0x22);
		colors.white = argb_pixel(0xff, 0xff, 0xff, 0xff);
		return colors;
	}

	colors.green = allocate_color(display, screen, "#00ff00",
				      WhitePixel(display, screen));
	colors.red = allocate_color(display, screen, "#cc2222",
				    BlackPixel(display, screen));
	colors.white = allocate_color(display, screen, "#ffffff",
				      WhitePixel(display, screen));

	return colors;
}

/*
 * Desenha a mira.
 *
 * A cruz e feita com quatro linhas pequenas e um espaco no centro. Esse espaco
 * ajuda a nao cobrir exatamente o ponto onde o usuario quer mirar.
 */
static void draw_crosshair(Display *display, Window window, GC gc,
			   unsigned long color)
{
	const int cx = CROSSHAIR_X;
	const int cy = CROSSHAIR_Y;

	XSetForeground(display, gc, color);
	XDrawLine(display, window, gc, cx - 12, cy, cx - 4, cy);
	XDrawLine(display, window, gc, cx + 4, cy, cx + 12, cy);
	XDrawLine(display, window, gc, cx, cy - 12, cx, cy - 4);
	XDrawLine(display, window, gc, cx, cy + 4, cx, cy + 12);
}

/*
 * Desenha o botao de fechar.
 *
 * Ele fica dentro da propria janela de overlay para que o usuario consiga sair
 * sem precisar matar o processo pelo terminal.
 */
static void draw_close_button(Display *display, Window window, GC gc,
			      const struct paint_colors *colors)
{
	struct point button = close_button_position();
	int right = button.x + CLOSE_BUTTON_SIZE - 7;
	int bottom = button.y + CLOSE_BUTTON_SIZE - 7;

	XSetForeground(display, gc, colors->red);
	XFillRectangle(display, window, gc, button.x, button.y,
		       CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE);

	XSetForeground(display, gc, colors->white);
	XDrawLine(display, window, gc, button.x + 6, button.y + 6,
		  right, bottom);
	XDrawLine(display, window, gc, right, button.y + 6,
		  button.x + 6, bottom);
}

/*
 * Redesenha tudo que aparece na janela.
 *
 * A funcao recebe apenas os dados necessarios para pintar; ela nao sabe nada
 * sobre loop de eventos ou ciclo de vida da aplicacao.
 */
static void draw_overlay(const struct app *app)
{
	XClearWindow(app->display, app->window);
	draw_crosshair(app->display, app->window, app->gc, app->colors.green);
	draw_close_button(app->display, app->window, app->gc, &app->colors);
}

/*
 * Cria a janela do overlay.
 *
 * override_redirect evita que o gerenciador de janelas coloque borda, barra de
 * titulo ou decoracao. XMapRaised, chamado depois, coloca a janela visivel e
 * acima das outras.
 */
static Window create_overlay_window(struct app *app)
{
	Window root = RootWindow(app->display, app->screen);
	Visual *window_visual;
	XSetWindowAttributes attrs;
	struct point position;
	int depth;

	if (app->visual) {
		depth = 32;
		window_visual = app->visual;
	} else {
		depth = DefaultDepth(app->display, app->screen);
		window_visual = DefaultVisual(app->display, app->screen);
	}

	app->colormap = XCreateColormap(app->display, root, window_visual,
					AllocNone);
	position = overlay_position(&app->monitor);

	attrs.background_pixel = 0;
	attrs.border_pixel = 0;
	attrs.colormap = app->colormap;
	attrs.override_redirect = True;
	attrs.event_mask = ExposureMask | ButtonPressMask | StructureNotifyMask;

	return XCreateWindow(app->display, root, position.x, position.y,
			     WINDOW_WIDTH, WINDOW_HEIGHT, 0, depth, InputOutput,
			     window_visual,
			     CWBackPixel | CWBorderPixel | CWColormap |
				     CWOverrideRedirect | CWEventMask,
			     &attrs);
}

/*
 * Inicializa todos os recursos do programa.
 *
 * Esta funcao e o ponto unico de montagem da aplicacao. Se algo falhar aqui,
 * main retorna erro e nenhuma outra parte precisa conhecer detalhes de setup.
 */
static bool app_init(struct app *app)
{
	app->display = XOpenDisplay(NULL);
	if (!app->display) {
		fprintf(stderr,
			"Erro: nao foi possivel conectar ao servidor X11.\n");
		return false;
	}

	app->screen = DefaultScreen(app->display);
	app->monitor = detect_first_monitor(app->display, app->screen);
	app->visual = find_argb_visual(app->display, app->screen);
	app->window = create_overlay_window(app);
	app->gc = XCreateGC(app->display, app->window, 0, NULL);
	app->colors = prepare_paint_colors(app->display, app->screen,
					   app->visual);
	app->running = true;

	XSetLineAttributes(app->display, app->gc, 3, LineSolid, CapRound,
			   JoinRound);
	XMapRaised(app->display, app->window);

	return true;
}

/*
 * Processa um evento recebido do X11.
 *
 * Expose pede redesenho porque a janela ficou visivel/precisa ser atualizada.
 * ButtonPress verifica se o clique foi no botao de fechar.
 */
static void app_handle_event(struct app *app, const XEvent *event)
{
	if (event->type == Expose) {
		draw_overlay(app);
		return;
	}

	if (event->type == ButtonPress &&
	    is_close_button_hit(event->xbutton.x, event->xbutton.y))
		app->running = false;
}

/*
 * Loop principal.
 *
 * Enquanto a aplicacao estiver ativa, consumimos todos os eventos pendentes,
 * redesenhamos o overlay, enviamos os comandos ao X11 e dormimos um pouco para
 * nao gastar CPU sem necessidade.
 */
static void app_run(struct app *app)
{
	while (app->running) {
		while (XPending(app->display) > 0) {
			XEvent event;

			XNextEvent(app->display, &event);
			app_handle_event(app, &event);
		}

		draw_overlay(app);
		XFlush(app->display);
		sleep_one_frame();
	}
}

/*
 * Libera recursos na ordem inversa da criacao.
 *
 * Em programas pequenos isso parece detalhe, mas manter uma funcao de cleanup
 * evita vazamentos quando o codigo crescer.
 */
static void app_destroy(struct app *app)
{
	if (!app->display)
		return;

	if (app->gc)
		XFreeGC(app->display, app->gc);

	if (app->window)
		XDestroyWindow(app->display, app->window);

	if (app->colormap)
		XFreeColormap(app->display, app->colormap);

	XCloseDisplay(app->display);
}

int main(void)
{
	struct app app = {0};

	if (!app_init(&app))
		return EXIT_FAILURE;

	app_run(&app);
	app_destroy(&app);

	return EXIT_SUCCESS;
}
