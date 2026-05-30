# crosshair

`crosshair` e um programa pequeno em C que mostra uma mira verde no centro do
primeiro monitor em ambientes Linux com X11.

Ele e apenas um overlay visual. O programa nao le memoria de jogos, nao injeta
codigo, nao altera arquivos do sistema e nao conversa com nenhum jogo. Ele so
abre uma janelinha transparente por cima da area de trabalho.

## Para que serve

Serve para estudar:

- programacao C em estilo simples e organizado;
- uso basico da biblioteca X11;
- criacao de janela sem borda;
- desenho de linhas e retangulos na tela;
- organizacao de codigo com responsabilidades pequenas.

## Como o programa funciona

De forma resumida:

1. O programa conecta no servidor grafico X11.
2. Ele descobre o tamanho e a posicao do primeiro monitor.
3. Ele cria uma janela transparente, sem borda e por cima das outras.
4. Ele desenha a mira e um botao vermelho de fechar no canto superior direito
   do primeiro monitor.
5. Ele fica em loop redesenhando a janela e lendo cliques do mouse.
6. Quando o botao vermelho recebe clique, o programa encerra.

## Dependencias

Voce precisa de um Linux usando X11 e dos arquivos de desenvolvimento da X11.

No Debian, Ubuntu e derivados:

```bash
sudo apt install build-essential libx11-dev x11-xserver-utils
```

`build-essential` instala compilador e `make`.
`libx11-dev` instala os cabecalhos e bibliotecas usados pelo codigo.
`x11-xserver-utils` normalmente fornece o comando `xrandr`, usado para detectar
monitores com mais precisao.

## Como compilar

Dentro da pasta do projeto, rode:

```bash
make
```

O objeto intermediario fica em `/tmp/crosshair-build`, mas o binario final fica
na raiz do projeto:

```bash
./crosshair
```

## Como executar

Depois de compilar:

```bash
./crosshair
```

Voce tambem pode compilar e executar com:

```bash
make run
```

## Como fechar

Clique no botao vermelho com `X` no canto superior direito do primeiro monitor.

Se precisar encerrar pelo terminal:

```bash
pkill crosshair
```

## Como limpar o build

```bash
make clean
```

Isso remove o binario `./crosshair` e a pasta de build:

```bash
/tmp/crosshair-build
```

## Estrutura dos arquivos

- `crosshair.c`: codigo-fonte comentado do programa.
- `Makefile`: regras para compilar, executar e limpar.
- `.gitignore`: lista de arquivos locais que nao devem ir para o Git.

## Observacoes importantes

- Este projeto foi feito para X11. Em uma sessao Wayland pura, ele pode nao
  aparecer ou pode se comportar diferente.
- A transparencia depende do compositor do ambiente grafico.
- O codigo esta em um unico arquivo porque o projeto e pequeno. Mesmo assim,
  as funcoes foram separadas por responsabilidade para facilitar estudo e
  manutencao.
