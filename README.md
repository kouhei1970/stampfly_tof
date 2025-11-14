# StampFly ToF ドライバ for ESP32-S3

M5StampFly用のVL53L3CX Time-of-Flight距離センサードライバです。ESP-IDF上で動作します。

**[English Documentation](README_EN.md)**

## 特徴

- ✅ **レジスタレベル完全実装** - VL53L3CXデータシートに基づく実装
- ✅ **デュアルセンサー対応** - 前方・底面2基のセンサー管理
- ✅ **MEDIUM_RANGEプリセットモード** - 0-3000mm測定範囲
- ✅ **ヒストグラムベース距離計算** - サブビン補間による高精度化
- ✅ **マルチセンサーI2Cアドレス管理** - XSHUTピン制御による自動設定
- ✅ **ESP-IDFネイティブ** - ESP32-S3最適化
- ✅ **連続測定モード** - 100ms測定間隔
- ✅ **割り込み対応** - GPIO割り込みによる効率的なデータ取得

## ハードウェア構成

### M5StampFlyピンマッピング

| 機能 | GPIO | 説明 |
|------|------|------|
| I2C SDA | GPIO3 | 共有I2Cバス |
| I2C SCL | GPIO4 | 共有I2Cバス |
| 前方XSHUT | GPIO9 | 前方センサーシャットダウン制御 |
| 前方INT | GPIO8 | 前方センサー割り込み（オプション） |
| 底面XSHUT | GPIO7 | 底面センサーシャットダウン制御 |
| 底面INT | GPIO6 | 底面センサー割り込み（オプション） |

### デフォルトI2Cアドレス

- **前方センサー**: `0x30` (初期化時にデフォルト `0x29` から変更)
- **底面センサー**: `0x31` (初期化時にデフォルト `0x29` から変更)

## クイックスタート

### 前提条件

- ESP-IDF v5.0以降
- M5StampFlyハードウェア

### インストール

1. ESP-IDFコンポーネントディレクトリにクローン:

```bash
cd ~/esp
git clone <repository-url> stampfly_tof
```

2. または既存プロジェクトのコンポーネントとして追加:

```bash
cd your_project/components
git clone <repository-url> stampfly_tof
```

### サンプルのビルド

#### ポーリングモード (basic)

```bash
cd stampfly_tof/exsampls/basic

# ESP-IDF環境設定
. $IDF_PATH/export.sh

# ビルド
idf.py build

# フラッシュ&モニター
idf.py -p /dev/ttyUSB0 flash monitor
```

#### 割り込みモード (interrupt)

```bash
cd stampfly_tof/exsampls/interrupt
. $IDF_PATH/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## 使い方

### 基本サンプル (ポーリングモード)

```c
#include "stampfly_tof.h"

void app_main(void)
{
    // ToFシステム初期化
    stampfly_tof_handle_t tof;
    ESP_ERROR_CHECK(stampfly_tof_init(&tof, I2C_NUM_0));

    // 連続測定開始
    ESP_ERROR_CHECK(stampfly_tof_start_ranging(&tof, STAMPFLY_TOF_SENSOR_BOTH));

    // メインループ
    while (1) {
        stampfly_tof_dual_result_t result;

        // 両センサーから距離取得
        if (stampfly_tof_get_dual_distance(&tof, &result) == ESP_OK) {
            if (result.front_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                printf("前方: %d mm\n", result.front_distance_mm);
            }
            if (result.bottom_status == VL53L3CX_RANGE_STATUS_RANGE_VALID) {
                printf("底面: %d mm\n", result.bottom_distance_mm);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 割り込みモードサンプル

リアルタイムアプリケーションでは、ポーリングの代わりにGPIO割り込みを使用することでより効率的に動作します:

```c
#include "stampfly_tof.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t data_ready_sem;

// 割り込みコールバック（ISRから呼ばれる）
static void IRAM_ATTR data_ready_callback(stampfly_tof_handle_t *handle,
                                           stampfly_tof_sensor_t sensor)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(data_ready_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void app_main(void)
{
    // セマフォ作成
    data_ready_sem = xSemaphoreCreateBinary();

    // ToFシステム初期化
    stampfly_tof_handle_t tof;
    stampfly_tof_init(&tof, I2C_NUM_0);

    // 割り込み有効化
    stampfly_tof_enable_interrupt(&tof, STAMPFLY_TOF_SENSOR_BOTH, data_ready_callback);

    // 測定開始
    stampfly_tof_start_ranging(&tof, STAMPFLY_TOF_SENSOR_BOTH);

    // 割り込み待機
    while (1) {
        if (xSemaphoreTake(data_ready_sem, portMAX_DELAY) == pdTRUE) {
            vl53l3cx_result_t result;
            if (vl53l3cx_get_ranging_data(&tof.front_sensor, &result) == ESP_OK) {
                printf("距離: %d mm\n", result.distance_mm);
            }
        }
    }
}
```

### 単一センサーサンプル

```c
#include "vl53l3cx.h"

void app_main(void)
{
    // I2C初期化
    vl53l3cx_i2c_master_init(I2C_NUM_0, GPIO_NUM_3, GPIO_NUM_4, 400000);

    // 単一センサー初期化
    vl53l3cx_dev_t sensor;
    vl53l3cx_init(&sensor, I2C_NUM_0, VL53L3CX_DEFAULT_I2C_ADDR);

    // 測定開始
    vl53l3cx_start_ranging(&sensor);

    // データ読み出し
    vl53l3cx_result_t result;
    while (1) {
        if (vl53l3cx_wait_data_ready(&sensor, 2000) == ESP_OK) {
            vl53l3cx_get_ranging_data(&sensor, &result);
            printf("距離: %d mm (ステータス: %s)\n",
                   result.distance_mm,
                   vl53l3cx_get_range_status_string(result.range_status));
        }
    }
}
```

## API リファレンス

### StampFly高レベルAPI

#### 初期化

```c
esp_err_t stampfly_tof_init(stampfly_tof_handle_t *handle, i2c_port_t i2c_port);
```

デュアルToFセンサーシステムを初期化します。以下の処理を実行:
1. I2Cマスター初期化
2. GPIO設定
3. 順次センサー初期化とアドレス変更

#### 測定制御

```c
esp_err_t stampfly_tof_start_ranging(stampfly_tof_handle_t *handle,
                                      stampfly_tof_sensor_t sensor);
```

選択したセンサーで連続測定を開始します。

- `sensor`: `STAMPFLY_TOF_SENSOR_FRONT`, `STAMPFLY_TOF_SENSOR_BOTTOM`, または `STAMPFLY_TOF_SENSOR_BOTH`

```c
esp_err_t stampfly_tof_stop_ranging(stampfly_tof_handle_t *handle,
                                     stampfly_tof_sensor_t sensor);
```

選択したセンサーの測定を停止します。

#### データ取得

```c
esp_err_t stampfly_tof_get_dual_distance(stampfly_tof_handle_t *handle,
                                          stampfly_tof_dual_result_t *result);
```

両方のセンサーから同時に距離を取得します。

```c
esp_err_t stampfly_tof_get_front_distance(stampfly_tof_handle_t *handle,
                                           vl53l3cx_result_t *result);
```

前方センサーのみから距離を取得します。

```c
esp_err_t stampfly_tof_get_bottom_distance(stampfly_tof_handle_t *handle,
                                            vl53l3cx_result_t *result);
```

底面センサーのみから距離を取得します。

#### 割り込みサポート

```c
esp_err_t stampfly_tof_enable_interrupt(stampfly_tof_handle_t *handle,
                                         stampfly_tof_sensor_t sensor,
                                         stampfly_tof_interrupt_callback_t callback);
```

データレディ検出用のGPIO割り込みを有効化します。ポーリングよりも効率的です。

- `callback`: データ準備完了時にISRから呼ばれる関数

```c
esp_err_t stampfly_tof_disable_interrupt(stampfly_tof_handle_t *handle,
                                          stampfly_tof_sensor_t sensor);
```

GPIO割り込みを無効化します。

### VL53L3CXコアAPI

#### デバイス初期化

```c
esp_err_t vl53l3cx_init(vl53l3cx_dev_t *dev, i2c_port_t i2c_port, uint8_t i2c_addr);
```

完全な初期化シーケンス:
- ファームウェア起動待ち
- NVM較正データ読み出し
- MEDIUM_RANGEプリセットモード設定

#### I2Cアドレス管理

```c
esp_err_t vl53l3cx_set_device_address(vl53l3cx_dev_t *dev, uint8_t new_addr);
```

I2Cアドレスを変更します（揮発性、電源オフでリセット）。

#### 測定制御

```c
esp_err_t vl53l3cx_start_ranging(vl53l3cx_dev_t *dev);
esp_err_t vl53l3cx_stop_ranging(vl53l3cx_dev_t *dev);
esp_err_t vl53l3cx_wait_data_ready(vl53l3cx_dev_t *dev, uint32_t timeout_ms);
esp_err_t vl53l3cx_check_data_ready(vl53l3cx_dev_t *dev, uint8_t *ready);
esp_err_t vl53l3cx_get_ranging_data(vl53l3cx_dev_t *dev, vl53l3cx_result_t *result);
```

#### ステータスユーティリティ

```c
const char* vl53l3cx_get_range_status_string(uint8_t status);
```

人間が読めるステータス文字列を返します。

## データ構造

### `vl53l3cx_result_t`

```c
typedef struct {
    uint16_t distance_mm;                // 測定距離 (mm)
    uint8_t range_status;                // レンジステータスコード
    uint8_t stream_count;                // 測定カウンタ
    uint32_t bin_data[24];               // 生ヒストグラムビン
    uint32_t ambient_estimate;           // アンビエント光レベル
    uint8_t peak_bin;                    // ピークビンインデックス
} vl53l3cx_result_t;
```

### `stampfly_tof_dual_result_t`

```c
typedef struct {
    uint16_t front_distance_mm;          // 前方センサー距離
    uint8_t front_status;                // 前方センサーステータス
    uint16_t bottom_distance_mm;         // 底面センサー距離
    uint8_t bottom_status;               // 底面センサーステータス
} stampfly_tof_dual_result_t;
```

## レンジステータスコード

| ステータスコード | 説明 |
|------------------|------|
| `0x09` | **Range Valid** - 測定成功 |
| `0x01` | Sigma Fail - 信号品質低下 |
| `0x02` | Signal Fail - ターゲット検出なし |
| `0x04` | Out of Bounds - ターゲットが遠すぎる/近すぎる |
| `0x05` | Hardware Fail - センサー故障 |

完全なステータスコードリストは `vl53l3cx.h` を参照してください。

## 技術詳細

### 初期化シーケンス

1. **ファームウェア起動** (~100-200ms)
   - `FIRMWARE_SYSTEM_STATUS` レジスタをポーリング
   - bit 0 = 1 まで待機

2. **NVM較正データ** (~5ms)
   - ファームウェア無効化
   - パワーフォース有効化
   - NVMからオシレータ周波数読み出し
   - ファームウェア復帰

3. **MEDIUM_RANGEプリセット** (~50レジスタ書き込み)
   - GPIO、SPAD、VCSELパラメータ設定
   - タイミング設定（VCSEL周期、タイムアウト）
   - ROI、閾値、アルゴリズム設定

4. **マルチセンサーセットアップ**
   - XSHUTピンで全センサーシャットダウン
   - センサー1を起動、初期化、アドレス0x30に変更
   - センサー2を起動、初期化、アドレス0x31に変更

### 距離計算

ドライバはヒストグラムベースの測定を実装:

1. **アンビエント除去**: 最初の6ビンを平均してアンビエント推定
2. **ピーク検出**: ビン6-17から最大信号を検出
3. **サブビン補間**: 放物線フィッティングによるサブビン精度
4. **ビン幅計算**:
   - Period A (ビン0-11): ~15.0 mm/bin
   - Period B (ビン12-23): ~12.5 mm/bin

距離計算式: `distance = (peak_bin + sub_bin_offset) × bin_width`

### パフォーマンス

- **測定時間**: ~33ms (MEDIUM_RANGEモード)
- **測定間隔**: 100ms (設定可能)
- **測定範囲**: 0-3000mm
- **精度**: ±5% 標準

## トラブルシューティング

### センサーが検出されない

- I2C配線を確認 (SDA=GPIO3, SCL=GPIO4)
- 電源供給を確認 (センサーは2.8V I/O必要)
- XSHUTピン接続を確認

### 距離測定が不正確

- ターゲットが0-3000mm範囲内にあることを確認
- アンビエント光の干渉をチェック
- ターゲットの反射率が十分であることを確認

### I2C通信エラー

- I2Cクロック速度を下げる (初期化時に `400000` を `100000` に変更)
- 外部プルアップ抵抗を追加 (4.7kΩ推奨)
- I2Cバスの競合をチェック

### 割り込みが動作しない

- INTピン接続を確認
- GPIO割り込み設定を確認
- ISRサービスがインストールされているか確認
- コールバック関数がIRAM_ATTR属性を持つことを確認

## 参考資料

- [VL53L3CXデータシート](docs/VL53L3CX_doc_ja.md)
- [M5StampFly仕様書](docs/M5StamFly_spec_ja.md)
- [ESP-IDFドキュメント](https://docs.espressif.com/projects/esp-idf/)

## ライセンス

このプロジェクトはM5StampFlyハードウェアでの使用を前提として提供されます。

## コントリビュート

コントリビューションを歓迎します！バグやfeatureリクエストは、プルリクエストまたはissueで提出してください。
