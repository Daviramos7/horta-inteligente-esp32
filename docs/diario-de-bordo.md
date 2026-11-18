# Diário de bordo — montagem de 17/11/2026

Registro dos problemas encontrados durante a montagem e a validação em bancada, com a causa e a
correção de cada um. Serve de base para a seção de testes e limitações do relatório.

## 1. Upload travado em `Connecting....`

- **Sintoma:** `Wrong boot mode detected (0x17)! The chip needs to be in download mode`.
- **Causa:** a placa não entra sozinha no modo de gravação.
- **Correção:** segurar o botão **BOOT** durante o `Connecting....` até aparecer `Writing at`.

## 2. Calibração do sensor de solo

| Situação | Leituras | Valor adotado |
|---|---|---|
| No ar | 3232–3347 | ~3290 |
| Na água | 991–1072 | ~1025 |

Faixa útil de ~2265 pontos do ADC. Limites para cacto: liga a ~15%, desliga a ~35%.

## 3. Relé sem energia

- **Sintoma:** relé sem LEDs e sem "tec"; multímetro mostrava 0 V entre VCC e GND do relé.
- **Causa:** o fio do VCC estava no pino **VN** (entrada analógica) em vez do **VIN** (5 V).
- **Correção:** mover para o VIN. Nenhum componente foi danificado.

## 4. Relé que não desliga

- **Sintoma:** LED verde aceso o tempo todo, sem "tec".
- **Diagnóstico:** soltar o fio do IN apagava o LED; o relé liga com **LOW**. O HIGH do ESP32
  (3,3 V) não basta para desligar um módulo alimentado com 5 V.
- **Tentativa 1:** desligar colocando o pino como entrada (`pinMode(INPUT)`). O LED ficava fraco,
  ainda sem desligar o contato.
- **Tentativa 2:** um diodo 1N4007 em série no IN. O primeiro foi colocado invertido e o relé
  parou de ligar; ao inverter, melhorou, mas o contato continuava preso (medido com multímetro).
- **Correção final:** **dois diodos em série** + `pinMode(INPUT)` para desligar. O multímetro
  passou a alternar `000`/`OL` entre COM e NO, com "tec" firme.
- **Lição:** o lado do diodo deve ser confirmado com o multímetro (escala de diodo) antes de
  montar, e não pela cor da fita da cartela.

## 5. Identificação dos bornes do relé

Os bornes vinham marcados em chinês: 常开 = NO, 公共端 = COM, 常闭 = NC. Confirmado com o
multímetro: COM–NO fecha com o relé ligado; COM–NC fecha com ele desligado.

## 6. Polaridade da fonte

O adaptador P4 tem ⊕ e ⊖ gravados. Medido com multímetro: +5 V com a ponta vermelha no ⊕.
Conferido antes de ligar a bomba.

## 7. DHT22 com `erro`

- **Sintoma:** `DHT erro` em todas as linhas depois de mexer na montagem.
- **Causa:** mau contato no fio do **D4**; voltou a funcionar ao recolocar.

## 8. Bomba recebe ON mas não gira

- **Sintoma:** Monitor Serial mostrava `Bomba: ON` e o relé fazia "tec", mas a bomba não girava.
- **Conclusão:** lógica e relé corretos; a falha estava no caminho de energia da bomba
  (fonte, emendas e contatos).
- **Status:** **resolvido** pela equipe. A bomba passou a regar acionada pelo relé, com o diodo
  e o capacitor de proteção instalados.

## 9. Interface e MQTT

- Primeira versão: painel local servido pelo próprio ESP32 (rede `HortaInteligente`).
- A proposta entregue ao professor previa **MQTT**. Foi adicionada a publicação em
  `broker.hivemq.com` e um painel online que recebe os dados por WebSocket.
- O painel local foi mantido: atende o requisito de funcionar sem internet.
- O Arduino IDE travou uma vez (`arduino-verify-sketch cannot be executed`, status "Offline");
  resolvido fechando e abrindo o IDE. Não era erro de código.

## 10. Calibração no substrato

Feita pela equipe com o sensor no vaso, em terra seca e após a rega, conforme previsto na
proposta. Os valores usados no firmware ficam nas constantes `SECO`, `MOLHADO`, `LIGA_ACIMA` e
`DESLIGA_ABAIXO`.

## 11. Validação do MQTT

O painel online (`dashboard/dashboard_online.html`) recebeu os dados publicados pelo ESP32 via
`broker.hivemq.com`, confirmando a comunicação MQTT de ponta a ponta.

## 12. Testes de falha

Realizados pela equipe:

- [x] Sensor de solo desconectado: a bomba não deve ligar.
- [x] Perda de Wi-Fi/internet: a rega local deve continuar e o painel online deve indicar a horta
  como offline.
- [x] Limite de tempo: com o sensor no ar, a bomba deve desligar sozinha em 10 s.
- [x] Inicialização: a bomba deve começar desligada.

## Situação final

- [x] Bomba funcionando pelo relé.
- [x] Comunicação MQTT validada com o painel online.
- [x] Calibração no substrato.
- [x] Testes de falha.
- [ ] Sensor de nível do reservatório: **não instalado**, registrado como limitação. O uso
  permanece supervisionado, com abastecimento manual.
- [ ] Acompanhamento de uma semana: **não realizado** por falta de tempo; registrado como
  limitação.
