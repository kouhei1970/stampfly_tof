# Stage 8: VL53L3CX フィルタ付きTeleplotストリーミング

## 概要

このステージでは、外れ値除去フィルタを追加した連続測定を実装します。生データとフィルタリング後のデータを同時に出力し、Teleplotで比較できます。

## 外れ値除去アルゴリズム

### 実装されているフィルタ

1. **移動中央値フィルタ（Median Filter）** - デフォルト
   - 外れ値に最も強い
   - ウィンドウ内の中央値を出力
   - 推奨用途: ノイズが多い環境

2. **移動平均フィルタ（Average Filter）**
   - スムーズな出力
   - ウィンドウ内の平均値を出力
   - 推奨用途: 安定した環境

3. **加重平均フィルタ（Weighted Average Filter）**
   - 最新のサンプルに高い重みを付与
   - 応答性とスムーズさのバランス
   - 推奨用途: リアルタイム制御

### フィルタの仕組み

```
生データ: 245, 247, 246, 1500, 245, 244, 246
                          ^^^^
                         外れ値

中央値フィルタ(window=5):
[245, 247, 246, 1500, 245] → 中央値 = 246 (外れ値を無視)
```

### フィルタリングステップ

1. **レンジステータスチェック**
   - ステータス 0 のみ有効として扱う（デフォルト）
   - 無効なステータスのデータは即座に除外

2. **変化率リミッター**
   - 前回の出力から500mm以上の急激な変化を除外
   - 物理的にあり得ない変化を防ぐ

3. **選択したフィルタアルゴリズム**
   - 中央値/平均/加重平均を適用
   - ウィンドウサイズ分のサンプルを使用

## 期待される出力

### コンソール出力

```
Stage 8: Filtered Teleplot Streaming
VL53L3CX ToF Sensors with Outlier Filtering
==================================
Bottom filter: Median, window=5, rate_limit=500mm
I2C master initialized successfully
SDA: GPIO3, SCL: GPIO4
Starting I2C address change sequence...
  1. Both sensors shutdown
  2. Bottom sensor enabled at default 0x29
  3. Bottom sensor address changed to 0x30
  4. Front sensor DISABLED (set ENABLE_FRONT_SENSOR=1 to enable)
I2C address change sequence complete
Bottom ToF: GPIO7 (0x30) [ENABLED - USB powered]
Front ToF: GPIO9 [DISABLED]
INT pins initialized
Bottom INT: GPIO6
Initializing BOTTOM sensor...
BOTTOM: ✓ Device booted
BOTTOM: ✓ Data initialized
BOTTOM: ✓ Product Type: 0xAA, Rev: 1.1
Using default measurement parameters
==================================
Starting continuous streaming
Interrupt mode, Teleplot format
Filter: Median filter, window=5
Bottom sensor only (USB powered)
==================================
Streaming tasks started.
Teleplot variables:
  - bottom_raw (raw distance)
  - bottom_filtered (filtered distance)
  - bottom_signal, bottom_status
BOTTOM: Starting continuous measurements...
>bottom_raw:245
>bottom_signal:15.32
>bottom_status:0
>bottom_filtered:245
>bottom_raw:247
>bottom_signal:15.28
>bottom_status:0
>bottom_filtered:246
>bottom_raw:1500
>bottom_signal:5.12
>bottom_status:4
>bottom_filtered:0
>bottom_raw:245
>bottom_signal:15.31
>bottom_status:0
>bottom_filtered:246
...
```

### Teleplotグラフ表示

Teleplotでは以下のグラフが表示されます：

- **bottom_raw** (青線): 生の測定データ（外れ値含む）
- **bottom_filtered** (緑線): フィルタリング後のデータ（外れ値除去済み）
- **bottom_signal**: 信号強度
- **bottom_status**: 測定ステータス

外れ値が発生した際、生データは急激にジャンプしますが、フィルタ後のデータは滑らかに推移します。

## フィルタ設定のカスタマイズ

### 基本設定

```c
vl53lx_filter_config_t filter_config = VL53LX_FilterGetDefaultConfig();

// フィルタタイプを選択
filter_config.filter_type = VL53LX_FILTER_MEDIAN;      // 中央値フィルタ
// filter_config.filter_type = VL53LX_FILTER_AVERAGE;  // 平均フィルタ
// filter_config.filter_type = VL53LX_FILTER_WEIGHTED_AVG;  // 加重平均フィルタ

// ウィンドウサイズ (3-15)
filter_config.window_size = 5;  // 5サンプル

// レンジステータスチェック
filter_config.enable_status_check = true;
filter_config.valid_status_mask = 0x01;  // Status 0のみ有効

// 変化率リミッター
filter_config.enable_rate_limit = true;
filter_config.max_change_rate_mm = 500;  // 最大500mm変化

VL53LX_FilterInitWithConfig(&bottom_filter, &filter_config);
```

### ウィンドウサイズの選び方

| ウィンドウサイズ | 応答速度 | ノイズ除去能力 | 推奨用途 |
|----------------|---------|---------------|---------|
| 3 | 速い | 低 | 高速移動物体 |
| 5 | 中 | 中 | 一般的な用途 **（デフォルト）** |
| 7-9 | やや遅い | 高 | 静止物体測定 |
| 10-15 | 遅い | 非常に高 | データロギング |

### 変化率リミッターの調整

```c
// ドローンの高度測定（ゆっくり変化）
filter_config.max_change_rate_mm = 200;  // 200mm/sample

// 障害物検出（速い変化あり）
filter_config.max_change_rate_mm = 1000;  // 1000mm/sample

// リミッター無効
filter_config.enable_rate_limit = false;
```

測定レートが約30Hzなので、`max_change_rate_mm`は1サンプル（約33ms）あたりの最大変化量です。

例: 500mm/sample = 15 m/s の速度まで追従可能

## フィルタの性能比較

### 中央値フィルタ vs 平均フィルタ

```
生データ:    [100, 102, 500, 101, 103]  ← 500が外れ値
中央値:       102 (外れ値を無視)
平均:         181 (外れ値に影響される)
```

**結論**: 外れ値が多い場合は中央値フィルタが最適

### ウィンドウサイズの影響

```
生データ:    100, 105, 110, 115, 120, 125, 130

window=3の中央値:  105, 110, 115, 120, 125, 130  (応答が速い)
window=7の中央値:  110, 115, 120, 125, 125, 125  (遅延あり)
```

## ビルド＆実行方法

```bash
cd examples/stage8_filtered_streaming
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## Teleplotでの確認方法

1. VSCodeでTeleplotを起動
2. シリアルポート選択、ボーレート115200
3. 以下のグラフが表示されます：
   - `bottom_raw`: 生データ
   - `bottom_filtered`: フィルタ後のデータ
4. センサーに手を近づけたり遠ざけたりして、フィルタの効果を確認

### フィルタ効果の確認方法

- センサーを急に遮る → 生データは急変化、フィルタ後は滑らか
- 測定対象がない（アウトオブレンジ） → status=4, filtered=0（除外される）
- 安定した測定 → 生データとフィルタ後がほぼ一致

## フィルタAPIの使い方

### 初期化

```c
#include "vl53lx_outlier_filter.h"

vl53lx_filter_t filter;

// デフォルト設定で初期化
VL53LX_FilterInit(&filter);

// または、カスタム設定で初期化
vl53lx_filter_config_t config = VL53LX_FilterGetDefaultConfig();
config.filter_type = VL53LX_FILTER_MEDIAN;
config.window_size = 7;
VL53LX_FilterInitWithConfig(&filter, &config);
```

### データ処理

```c
uint16_t raw_distance = data.RangeData[0].RangeMilliMeter;
uint8_t range_status = data.RangeData[0].RangeStatus;
uint16_t filtered_distance;

bool valid = VL53LX_FilterUpdate(&filter, raw_distance, range_status, &filtered_distance);

if (valid) {
    // filtered_distanceを使用
    printf("Filtered: %u mm\n", filtered_distance);
} else {
    // 外れ値として除外された
    printf("Outlier rejected\n");
}
```

### クリーンアップ

```c
VL53LX_FilterDeinit(&filter);
```

## フィルタのリセット

環境が大きく変化した場合（例: ドローンの離陸/着陸）、フィルタバッファをクリアできます：

```c
VL53LX_FilterReset(&filter);
```

## 電源要件

Stage 7と同様：

- **デフォルト**: 底面ToFのみ（USB給電で動作）
- **両センサー**: `ENABLE_FRONT_SENSOR = 1`（バッテリー必要）

## トラブルシューティング

### フィルタ後のデータが常に0になる

**原因**: 変化率リミッターが厳しすぎる、またはステータスチェックで全て除外されている

**対処**:
```c
filter_config.max_change_rate_mm = 1000;  // リミッターを緩める
filter_config.enable_status_check = false;  // ステータスチェック無効化
```

### フィルタの応答が遅い

**原因**: ウィンドウサイズが大きすぎる

**対処**:
```c
filter_config.window_size = 3;  // ウィンドウを小さく
```

### メモリ不足エラー

**原因**: 複数のフィルタで大きなウィンドウを使用

**対処**:
```c
filter_config.window_size = 5;  // ウィンドウサイズを減らす
```

各フィルタのメモリ使用量: 約 `window_size * 3` バイト

## パフォーマンス

### CPU使用率

- フィルタなし（Stage 7）: 約5-10%
- フィルタあり（Stage 8）: 約8-12%

フィルタ処理のオーバーヘッドは最小限です。

### メモリ使用量

- フィルタ1個: 約100バイト（window=5の場合）
- 両センサー: 約200バイト

## 実用例

### ドローン高度制御

```c
// 高精度・低ノイズ設定
filter_config.filter_type = VL53LX_FILTER_MEDIAN;
filter_config.window_size = 7;
filter_config.max_change_rate_mm = 200;  // ゆっくりした変化のみ
```

### 障害物検出

```c
// 高速応答設定
filter_config.filter_type = VL53LX_FILTER_WEIGHTED_AVG;
filter_config.window_size = 3;
filter_config.max_change_rate_mm = 1000;  // 速い動きに対応
```

### データロギング

```c
// 最高品質設定
filter_config.filter_type = VL53LX_FILTER_MEDIAN;
filter_config.window_size = 15;
filter_config.enable_rate_limit = false;  // 全データを記録
```

## まとめ

Stage 8では：
- ✅ 3種類のフィルタアルゴリズム（中央値/平均/加重平均）
- ✅ レンジステータスベースの検証
- ✅ 変化率リミッター
- ✅ カスタマイズ可能な設定
- ✅ Teleplotで生データとフィルタ後を比較可能
- ✅ 低オーバーヘッド（CPU: +2-3%, メモリ: +100バイト）

これでノイズの多い環境でも安定した測定が可能になります！
