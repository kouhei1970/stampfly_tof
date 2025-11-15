# StampFly ToF Sensor Driver (VL53L3CX)

ESP32-S3ベースのM5Stamp Fly用VL53L3CX Time-of-Flight (ToF)距離センサードライバ

## 概要

このプロジェクトは、StampFly（M5Stamp S3搭載クアッドコプター）に搭載されたVL53L3CX ToFセンサーを使用するための最小構成ドライバとサンプルコードを提供します。

### 主な特徴

- ESP-IDF向けに最適化されたコンポーネント構成
- 段階的な学習・テストが可能なサンプルコード
- 距離測定に必要な最小限のAPIのみを抽出
- タイミングバジェット33ms対応
- 2センサー同時使用対応（前方/底面）

## ハードウェア仕様

### StampFly ToFセンサー配置

| センサー | XSHUT | INT | I2Cアドレス（デフォルト） |
|---------|-------|-----|------------------------|
| 前方ToF | GPIO9 | GPIO8 | 0x29 (7-bit) |
| 底面ToF | GPIO7 | GPIO6 | 0x29 (7-bit) → 0x30に変更 |

### I2C接続

- SDA: GPIO3
- SCL: GPIO4
- 周波数: 400kHz（推奨）

## プロジェクト構成

```
stampfly_tof/
├── CMakeLists.txt              # コンポーネント定義
├── Kconfig                     # 設定オプション
├── include/                    # ヘッダーファイル
│   ├── stampfly_tof_config.h   # 設定ヘッダー
│   └── vl53lx/                 # VL53LX公式ヘッダー（今後追加）
├── src/                        # ソースファイル
│   ├── stampfly_tof.c          # StampFly専用API（今後追加）
│   ├── vl53lx_platform.c       # プラットフォーム層（今後追加）
│   └── vl53lx/                 # VL53LXコアドライバ（今後追加）
├── examples/                   # サンプルプロジェクト（独立実行可能）
│   ├── stage1_i2c_scan/        # ✅ Stage 1: I2Cバススキャン
│   │   ├── CMakeLists.txt      #    プロジェクト定義
│   │   ├── main/               #    メインコンポーネント
│   │   │   ├── CMakeLists.txt
│   │   │   └── main.c
│   │   └── README.md
│   ├── stage2_register_test/   # 🔲 Stage 2: レジスタ読み書き
│   ├── stage3_device_init/     # 🔲 Stage 3: デバイス初期化
│   ├── stage4_polling/         # 🔲 Stage 4: ポーリング測定
│   ├── stage5_interrupt/       # 🔲 Stage 5: 割り込み測定
│   └── stage6_dual_sensor/     # 🔲 Stage 6: 2センサー統合
├── docs/                       # メーカードキュメント
└── README.md                   # このファイル
```

## 使用方法

### 方法1: サンプルプロジェクトを直接実行（推奨）

`examples/` フォルダ内の各ステージは独立したESP-IDFプロジェクトです。各フォルダ内で直接ビルド・実行できます。

```bash
# Stage 1に移動
cd examples/stage1_i2c_scan

# ESP-IDF環境を有効化
. ~/esp/esp-idf/export.sh

# ターゲット設定（初回のみ）
idf.py set-target esp32s3

# ビルド
idf.py build

# フラッシュとモニタ
idf.py flash monitor
```

### 方法2: 自分のプロジェクトでコンポーネントとして使用

このフォルダをあなたのESP-IDFプロジェクトの `components/` ディレクトリにコピーします。

```bash
# 例：新しいプロジェクトを作成
idf.py create-project my_tof_project
cd my_tof_project

# stampfly_tofコンポーネントをコピー
cp -r /path/to/stampfly_tof components/

# main/CMakeLists.txtでstampfly_tofを要求
# idf_component_register(
#     SRCS "main.c"
#     INCLUDE_DIRS "."
#     REQUIRES stampfly_tof  # <- 追加
# )

# ビルド
idf.py build flash monitor
```

## 段階的実装ガイド

### ✅ Stage 1: I2Cバススキャン

**目的**: I2C通信の基本動作確認とVL53L3CX検出

**内容**:
- I2Cマスター初期化
- バススキャン（0x03～0x77）
- XSHUTピン制御

**期待結果**:
```
Device found at address 0x29
  -> VL53L3CX detected at default address!
```

**詳細**: [examples/stage1_i2c_scan/README.md](examples/stage1_i2c_scan/README.md)

**ビルドテスト**: ✅ 成功

### 🔲 Stage 2: レジスタ読み書きテスト（未実装）

**目的**: VL53L3CXレジスタへの直接アクセス確認

**内容**:
- プラットフォーム層実装
- Model ID / Module Type レジスタ読み出し

**期待結果**:
- Model ID = 0xEA
- Module Type = 0xCC

### 🔲 Stage 3: デバイス初期化（未実装）

**目的**: VL53LX API初期化シーケンス確認

**内容**:
- VL53LXドライバコア実装
- デバイス初期化
- デバイス情報取得

### 🔲 Stage 4: ポーリング測定（未実装）

**目的**: 基本的な距離測定（タイミングバジェット33ms）

**内容**:
- 測定開始
- ポーリングによるデータ取得
- 距離表示

### 🔲 Stage 5: 割り込み測定（未実装）

**目的**: GPIO割り込みによる効率的なデータ取得

**内容**:
- 割り込みハンドラ実装
- FreeRTOSセマフォ使用
- 低消費電力動作

### 🔲 Stage 6: 2センサー統合（未実装）

**目的**: 前方・底面センサーの同時動作

**内容**:
- I2Cアドレス変更シーケンス
- 複数センサー管理
- 個別割り込み処理

## 設定オプション

`idf.py menuconfig` から設定を変更できます。

```
Component config → StampFly ToF Sensor Configuration
```

主な設定項目：
- I2C SDA/SCL GPIO番号
- I2C周波数
- ToFセンサーXSHUT/INT GPIO番号
- タイミングバジェット（8～500ms）

## 現在のステータス

- ✅ プロジェクト構成確立（examples内で独立実行可能）
- ✅ CMakeLists.txt / Kconfig作成
- ✅ Stage 1サンプル実装・ビルド確認
- ✅ Stage 1独立プロジェクト化完了
- 🔲 Stage 2以降のサンプル実装予定
- 🔲 VL53LXドライバ最小構成の抽出予定

## 次のステップ

1. Stage 1をハードウェアでテスト
2. Stage 2のプラットフォーム層実装
3. 必要なVL53LXドライバファイルの抽出
4. Stage 3～6の順次実装

## 参考ドキュメント

- [VL53L3CX Driver Documentation](docs/VL53L3CX_driver_doc.md)
- [M5StamFly Specifications](docs/M5StamFly_spec_ja.md)
- [VL53L3CX BareDriver (Manufacturer)](docs/VL53L3CX_BareDriver_1.2.14/)

## ライセンス

このプロジェクトのオリジナル部分はMITライセンスです。
VL53L3CX Bare Driverは STMicroelectronics のライセンスに従います（GPL-2.0+ OR BSD-3-Clause）。

## 貢献

バグ報告や機能追加の提案は Issues からお願いします。
