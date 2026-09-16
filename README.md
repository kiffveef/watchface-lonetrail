# Lonetrail

Pebble Time 2 (emery, 200x228 カラー) 向けウォッチフェイス。C言語 + Pebble SDK で実装。

## セットアップ

```sh
# システム依存 (QEMUエミュレータ用)
sudo apt install nodejs npm libsdl2-2.0-0 libglib2.0-0 libpixman-1-0 zlib1g libsndio7.0

# Pebble CLI と SDK
uv tool install pebble-tool
pebble sdk install latest
```

## ビルドと実行

```sh
pebble build                                        # build/watchface-lonetrail.pbw を生成
pebble install --emulator emery                     # emery エミュレータで起動
pebble screenshot --no-open --emulator emery shot.png
pebble install --phone <ip>                         # 実機へインストール
```

## 構成

```
src/c/main.c     ウォッチフェイス本体
resources/       画像・フォント等 (package.json の resources.media に登録)
package.json     メタデータ (UUID, targetPlatforms, resources)
wscript          ビルド定義 (通常は編集不要)
```

## 参考

- SDK ドキュメント: <https://developer.repebble.com>
- 公式エージェントスキル: <https://github.com/coredevices/pebble-watchface-agent-skill>
