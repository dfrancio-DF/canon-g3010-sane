# Impressão em preto e branco na Canon G3010 / G3111 (sem converter o PDF)

Este documento complementa o README principal do repositório, que cobre o
**scanner** da mesma impressora. Aqui tratamos da **impressão**.

## Resumo

Em uma Canon G3111 (família **G3010 series**) conectada por Wi-Fi/IPP, os modos
`Black and White` e `Grayscale` do PPD genérico `pwgrast.ppd` produzem,
respectivamente, **folha em branco** e **folha toda preta/invertida**.

A solução é **não usar os modos monocromáticos do PPD**. Em vez disso, enviar o
atributo IPP padrão `print-color-mode=monochrome` e manter o raster em
`Rgb_8` (`PwgRasterDocumentType=Rgb_8`). A impressora converte para cinza
internamente, com foto e texto corretos, **sem converter o PDF antes**.

```bash
# padrão da fila (vale para todos os usuários que usam a fila)
lpadmin -p Canon-G3111 -o print-color-mode-default=monochrome \
                       -o PwgRasterDocumentType-default=Rgb_8

# padrão do usuário (garante que os diálogos gráficos herdem as opções)
mkdir -p ~/.cups
cp printing/lpoptions ~/.cups/lpoptions
```

O arquivo [`printing/lpoptions`](printing/lpoptions) já está pronto; ajuste o
nome da fila nele se a sua fila tiver outro nome.

Depois disso, imprimir normalmente:

```bash
lp -d Canon-G3111 documento.pdf
```

ou pelo diálogo gráfico do aplicativo — o P&B já vem como padrão.

---

## Ambiente onde foi validado

| Item | Valor |
|---|---|
| Impressora | Canon G3111 / `Canon G3010 series` |
| IP | `192.168.1.124` |
| URI | `ipp://192.168.1.124:631/ipp/print` |
| MAC | `34:9f:7b:10:8a:aa` (OUI Canon) |
| mDNS | `108AAA000000.local` |
| CUPS | `2.4.16-1ubuntu1.3` |
| cups-filters | `2.0.1-0ubuntu4.1` |
| PPD | `drv:///cupsfilters.drv/pwgrast.ppd` |
| Fila | `Canon-G3111` |

---

## Sintomas

No diálogo de impressão:

- **Black and White / `Black_1`** → folha sai **em branco**.
- **Grayscale / `SGray_8`** → folha sai **toda preta/invertida**.
- **Color (8 bit RGB) / `Rgb_8`** → imprime corretamente, mas em cor.

Também observado: `lpadmin -m everywhere` falha com
`Unable to create PPD: No IPP attributes.`

---

## Diagnóstico

### 1. A impressora anuncia suporte a monochrome

Pedido IPP enxuto (`printing/testes/gpa.test`):

```
document-format-supported        = application/octet-stream, image/pwg-raster
print-color-mode-supported        = color, monochrome, auto, auto-monochrome
```

Ou seja: **a impressora sabe fazer P&B por conta própria**. O problema está no
PPD, não nela.

### 2. O PPD genérico oferece modos que a impressora não tem

Pedido IPP completo (`printing/testes/attrs.test`):

```
pwg-raster-document-type-supported = srgb_8, sgray_8
output-mode-supported              = color, monochrome, auto, auto-monochrome
printer-make-and-model             = Canon G3010 series
page-ranges-supported              = false
```

O PPD `pwgrast.ppd` oferece:

```
Black_1  SGRay_1  Black_8  SGray_8  Black_16  SGray_16
Rgb_8    SRgb_8   AdobeRgb_8 ...
```

A impressora **só aceita `srgb_8` e `sgray_8`**. Os modos `black_*` são
inventados pelo PPD genérico → folha em branco. O `sgray_8` do PPD usa
`cupsColorSpace 18` (SGray) e esta firmware interpreta a polaridade invertida
→ folha toda preta.

### 3. `-m everywhere` falha por bug de firmware

```console
$ lpadmin -p Canon-G3111-D -E -v ipp://192.168.1.124:631/ipp/print -m everywhere
lpadmin: Unable to create PPD: No IPP attributes.
```

Motivo: o CUPS pede `Get-Printer-Attributes` com `requested-attributes=all,media-col-database`
e a impressora **não responde** (conexão fechada). O mesmo pedido com uma lista
enxuta de atributos funciona normalmente. Isso impede a criação do PPD
driverless, então a fila teve que ser criada com o PPD genérico:

```bash
lpadmin -p Canon-G3111 -E -v ipp://192.168.1.124:631/ipp/print \
        -P /usr/share/ppd/cupsfilters/pwgrast.ppd
```

### 4. O backend IPP do CUPS entende `print-color-mode`

```console
$ strings /usr/lib/cups/backend/ipp | grep -i color
print-color-mode
monochrome
print-color-mode-supported
DEBUG: adjust_options(): Adjusting print-color-mode to monochrome.
```

Confirmado também em `cupsd` e `libcups.so.2`. Portanto, basta passar o
atributo; o backend ajusta e a impressora faz a conversão.

---

## A correção

Enviar `print-color-mode=monochrome` junto com `PwgRasterDocumentType=Rgb_8`:

```bash
lp -d Canon-G3111 \
   -o print-color-mode=monochrome \
   -o PwgRasterDocumentType=Rgb_8 \
   -o media=A4 \
   documento.pdf
```

Tornando permanente:

```bash
# padrão da fila (vale para todos os usuários que usam a fila)
lpadmin -p Canon-G3111 -o print-color-mode-default=monochrome \
                       -o PwgRasterDocumentType-default=Rgb_8

# padrão do usuário (garante que os diálogos gráficos herdem as opções)
mkdir -p ~/.cups
cp printing/lpoptions ~/.cups/lpoptions
```

Verificação:

```console
$ lpoptions -p Canon-G3111 | tr ' ' '\n' | grep -iE 'color|raster|media'
media=A4
print-color-mode=monochrome
PwgRasterDocumentType=Rgb_8
```

---

## Validação

1. Folha de teste com o texto `TESTE MONOCHROME`, enviada com
   `print-color-mode=monochrome` + `Rgb_8` → saiu **preto sobre fundo branco**,
   corretamente (o "sentido paisagem" era do PDF de teste 600×200, não do driver).
2. Documento real com foto e texto
   (`CPF_RG_DiegoFrancio_DIGITAL.pdf`, job `Canon-G3111-191`) enviado **sem
   nenhuma opção**, apenas com os padrões acima → **imprimiu corretamente em
   preto e branco**, com foto e texto preservados. ✅

---

## Problema acessório: spool da impressora enchendo

Em vários momentos a impressora reportou:

```
printer-is-accepting-jobs = false
printer-state-reasons     = spool-area-full-report
printer-state             = processing
```

E cancelava trabalhos. Foi possível destravar via IPP, cancelando o job
travado no lado dela (`printing/testes/canceljob.test`, job-id 20):

```bash
ipptool -tv ipp://192.168.1.124:631/ipp/print printing/testes/canceljob.test
```

Depois disso ela voltou para `idle` / `printer-is-accepting-jobs=true` /
`printer-state-reasons=none`. Se isso recorrir, **desligar e tirar da tomada
por ~30 s** limpa o spool.

A impressora também oscilava na rede (`Host está desligado`, `Não há rota para
o host`), o que deixava jobs presos na fila até ela voltar.

---

## Workaround anterior (não mais necessário)

Antes de descobrir o `print-color-mode`, convertíamos o PDF para cinza com
Ghostscript e enviávamos em `Rgb_8`. O script está em `imprimir-pb`:

```bash
./imprimir-pb arquivo.pdf
```

Funcionou para vários documentos, mas **hoje a correção acima é preferível**:
imprime direto, sem gerar arquivo intermediário.

---

## Relação com bugs upstream

- [OpenPrinting/cups-filters#186](https://github.com/OpenPrinting/cups-filters/issues/186)
  — `ppdgenerator`: impressoras Canon que reportam apenas `srgb_8` em
  `pwg-raster-document-type-supported` ficam sem modos cinza no PPD gerado.
  O mantenedor comenta que vários fabricantes dizem suportar PWG Raster mas
  não implementam corretamente.
- [OpenPrinting/cups-filters@e414b73](https://github.com/OpenPrinting/cups-filters/commit/e414b73493)
  — `libppd`: corrige PPDs driverless removendo a geração artificial de
  `black_1` / `sgray_8`, exatamente os modos que falham aqui.

---

## Arquivos incluídos

| Arquivo | Uso |
|---|---|
| `PRINTING.md` | Este diagnóstico e a solução |
| `printing/lpoptions` | Padrões prontos para `~/.cups/lpoptions` |
| `printing/imprimir-pb` | Script de conversão prévia (workaround legado) |
| `printing/testes/gpa.test` | Consulta IPP enxuta (funciona nesta firmware) |
| `printing/testes/attrs.test` | Consulta IPP completa com os atributos relevantes |
| `printing/testes/getjobs.test` | Lista jobs no lado da impressora |
| `printing/testes/canceljob.test` | Cancela job travado (editar o `job-id`) |

Exemplo de uso dos testes:

```bash
ipptool -tv ipp://192.168.1.124:631/ipp/print printing/testes/gpa.test
ipptool -tv ipp://192.168.1.124:631/ipp/print printing/testes/attrs.test
ipptool -tv ipp://192.168.1.124:631/ipp/print printing/testes/getjobs.test
```
