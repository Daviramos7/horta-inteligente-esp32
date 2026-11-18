# Montagem passo a passo

Guia para remontar o protótipo do zero, com a posição exata de cada furo da protoboard.
**Faça todas as ligações com o USB e a fonte desconectados.**

## Convenções

- **Faixas laterais**: colunas compridas nas bordas da protoboard, marcadas com linha vermelha (+)
  e azul (−). Usamos só as da **esquerda**, para o lado do ESP32.
- **Furos centrais**: identificados por letra + número (ex.: `f40`). Os 5 furos da mesma linha
  (a–e ou f–j) são ligados entre si por dentro.
- **Jumper macho** tem pino; **fêmea** tem buraco. O ESP32 deste projeto fica **fora** da
  protoboard, então as ligações nos pinos dele usam a ponta fêmea.
- Os pinos do ESP32 são identificados pelo nome impresso na placa. Atenção: **VIN** e **VN** são
  pinos diferentes.

## 1. Alimentação dos sensores (lado esquerdo)

| De | Para |
|---|---|
| ESP32 **3V3** | faixa vermelha (+) esquerda |
| ESP32 **GND** (lado do 3V3) | faixa azul (−) esquerda |

## 2. DHT22

| Pino do módulo | Para |
|---|---|
| **+** | faixa vermelha (+) esquerda |
| **−** | faixa azul (−) esquerda |
| **out** | ESP32 **D4** |

## 3. Sensor capacitivo de solo

| Fio | Para |
|---|---|
| Preto (GND) | faixa azul (−) esquerda |
| Vermelho (VCC) | faixa vermelha (+) esquerda |
| Amarelo (AOUT) | ESP32 **D34** |

Enterre o sensor **só até a linha branca**: a parte de cima tem componentes que não podem molhar.

## 4. Relé (lado de comando)

| Pino do relé | Para |
|---|---|
| **VCC** | ESP32 **VIN** (1º pino ao lado do USB) |
| **GND** | ESP32 **GND** (2º pino, ao lado do VIN) |
| **IN** | jumper até `b20` (ver os diodos abaixo) |

Diodos no sinal (dois 1N4007 em série, **faixas apontando para o lado do D26**):

| Peça | Furo |
|---|---|
| Diodo 1, perna **sem** faixa | `a20` |
| Jumper vindo do **IN** do relé | `b20` |
| Diodo 1, perna **com** faixa | `a24` |
| Diodo 2, perna **sem** faixa | `b24` |
| Diodo 2, perna **com** faixa | `b28` |
| Jumper indo para o **D26** do ESP32 | `c28` |

Teste: com o firmware rodando, o relé deve fazer "tec" e o LED verde deve **apagar por completo**
quando desligado. Se o LED verde nunca acender, algum diodo está invertido.

## 5. Bomba (lado de potência, metade direita)

Fonte: plug P4 no adaptador com borne. Antes de tudo, meça com o multímetro (DCV 20) os bornes do
adaptador e confirme qual é o **⊕** (deve dar ~+5 V com a ponta vermelha nele).

| Peça | Para |
|---|---|
| Jumper do borne **⊕** do adaptador | borne **COM** do relé (meio, 公共端) |
| Jumper do borne **NO** do relé (常开) | `f40` |
| Fio **vermelho (+)** da bomba | `g40` |
| Diodo 1N4007, perna **com** faixa | `h40` |
| Capacitor 470 µF, perna **longa** (+) | `i40` |
| Fio **preto (−)** da bomba | `g44` |
| Diodo 1N4007, perna **sem** faixa | `h44` |
| Capacitor 470 µF, perna **curta** (−) | `i44` |
| Jumper do borne **⊖** do adaptador | `f44` |

Os bornes do relé: **NO** fecha quando o relé liga (é o que usamos); **NC** fica sem nada.
Confirme com o multímetro na escala de diodo: COM + NO deve mostrar `000` com o relé ligado e
`OL` com ele desligado.

Antes de ligar a fonte na tomada, com o ESP32 desligado, meça COM do relé contra `f44`: deve dar
`OL`. Se der `000`, há curto: não ligue.

## 6. Parte hidráulica

1. Corte uma garrafa PET de 2 L a ~15 cm do fundo.
2. Prenda a mangueira na saída da bomba.
3. Deite a bomba no fundo e encha até cobri-la com 3–4 dedos de água.
4. Deixe as emendas elétricas **fora** da água; só o cabo original da bomba entra.
5. Deixe o reservatório **no mesmo nível ou abaixo** do vaso, para evitar sifão.
6. Coloque um prato sob o vaso, que tem furo de drenagem.

## 7. Energia para funcionar sem o computador

- ESP32: qualquer carregador de celular 5 V, pelo cabo USB-C.
- Bomba: a fonte 5 V própria, pelo relé.

Duas fontes separadas evitam que o ruído da bomba reinicie o ESP32.
