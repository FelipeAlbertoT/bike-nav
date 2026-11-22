# BikeNav

Visor de guidão para a tela ST7789 240×240 no ESP32-C3 Mini. O iPhone, com o app **Sygic GPS Navigation & Maps**, calcula a rota e manda a próxima curva por Bluetooth. A tela desenha o trecho até essa curva: você embaixo, a rua à frente e a manobra se aproximando conforme a distância cai.

O Sygic não envia o desenho do caminho inteiro até o destino, então a linha na tela é só o próximo trecho, não o percurso completo. A rota continua sendo a de carro (ou moto) do Sygic.

## Ligar o Sygic

O modo BLE HUD é escondido e não oficial. Ele desliga quando o Sygic é encerrado e pode sumir numa atualização do app.

1. Abra o Sygic e permita o Bluetooth.
2. Ajustes → Info → Sobre.
3. Toque três vezes em qualquer item. Aparece uma linha extra de About.
4. About → BLE HUD → Start.
5. Deixe o app em primeiro plano até a tela do guidão sair de "Aguardando Sygic".
6. Com a rota ativa e o visor já conectado, a tela do iPhone pode apagar.

O aparelho anuncia o nome `ESP32 HUD`. O Sygic acha pelo serviço BLE, não pelo nome. Se o app reiniciar, repita o passo do BLE HUD.

Enquanto a rota não muda, o Sygic fica em silêncio. A cada 4 s sem pacote o visor pede um reenvio.

## Teste sem o iPhone

Monitor serial em 115200, USB CDC On Boot ligado.

- `demo` gira as setas, as distâncias e o limite.
- `stop` volta para "Aguardando Sygic".

Um pacote real do Sygic interrompe o demo.

## Fiação da tela

A mesma do `bringup_tft` que já acende este módulo. No conector da tela, SCL é o clock SPI e SDA é MOSI.

- TFT VCC → 5 V
- TFT GND → GND
- TFT SCL → GPIO4
- TFT SDA → GPIO6
- TFT CS → GPIO7
- TFT DC → GPIO10
- TFT RES → GPIO3
- TFT BLK → GPIO5

Brilho inicial: 180 de 255 (`TFT_BRIGHTNESS` em `bike-nav.ino`). Baixar para cerca de 100 aproxima a autonomia das 5–7 h citadas abaixo, e escurece a tela no sol. Se o branco nascer preto, mude `TFT_INVERT` para `false`.

## Alimentação

Duas células 3,7 V de 500 mAh em **paralelo** (1S2P, cerca de 1000 mAh). Nunca em série: 7,4 V queima a placa. Iguale a tensão das duas antes de unir.

Use um TP4056 com proteção e um boost regulado em 5,0 V no pino 5 V do C3. LiPo direto no pino 3V3 (a célula chega a 4,2 V) danifica o ESP32. No pino 5 V, sem o boost, o diodo e o regulador da placa derrubam os 3,3 V no fim da carga.

Consumo estimado 100–140 mA com o backlight por volta de 40%: cerca de 5–7 h nestas duas células. O GPS do iPhone gasta a bateria do telefone à parte.

## Gravar

No Arduino IDE: Ferramentas → Placa → esp32 → **ESP32C3 Dev Module**, e Ferramentas → USB CDC On Boot → **Enabled**. Se a placa continuar em Uno ou NodeMCU (ESP8266), a compilação para em `BLEDevice.h: No such file or directory` — esse arquivo só vem no núcleo esp32.

Pela linha de comando, o núcleo `esp32` 3.3.12 já traz o BLE (NimBLE no C3) e a LovyanGFX já está instalada.

```
arduino-cli board list
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc,CPUFreq=160,FlashSize=4M,PartitionScheme=default,UploadSpeed=921600 --upload -p COM5 E:\repo\arduino\bike-nav
```

Troque `COM5` pela porta que aparecer em `board list`.
