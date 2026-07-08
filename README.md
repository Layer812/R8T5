# RONTO8 for M5Stack CoreS3 (v0.62)

![RONTO8 - Touhou](toho.gif)

RONTO8 is an experimental [PICO-8](https://www.lexaloffle.com/pico-8.php) compatible fantasy console emulator specially designed and optimized for the [**M5Stack CoreS3**](https://docs.m5stack.com/ja/core/CoreS3). It is based on [femto8](https://github.com/benbaker76/femto8) and [zepto8](https://github.com/samhocevar/zepto8), bringing the joy of portable PICO-8 gaming and coding to this powerful device.

RONTO8は、[**M5Stack CoreS3**](https://docs.m5stack.com/ja/core/CoreS3) 専用に設計および最適化された [PICO-8](https://www.lexaloffle.com/pico-8.php) 互換のファンタジーコンソールエミュレータ（実験バージョン）です。[femto8](https://github.com/benbaker76/femto8) と [zepto8](https://github.com/samhocevar/zepto8) をベースにしており、このパワフルなデバイスでPICO-8のゲームやコーディングの楽しさを持ち歩くことができます。

---

## ⚠️ Disclaimer / 注意事項

- **Experimental Release / 実験的なリリース**: This is highly experimental code. If you find bugs, please report them gently! (実験的なコードであるため、バグを見つけた場合は優しく教えてください！)
- **Memory Limitations / メモリ制限**: Due to the hardware limitations, some very large PICO-8 cartridges may not work or may cause "not enough memory" errors during Lua compilation. (ハードウェア制限により、非常に大きいカートリッジは動作しないか、コンパイル時にメモリ不足エラーになる場合があります。)

---

## 🌟 Key Features / 主な機能

- **Optimized for M5Stack CoreS3**: Full support for the CoreS3's display and touch screen as a virtual gamepad.
  - CoreS3のディスプレイとタッチスクリーン（仮想ゲームパッド）に完全対応。
- **SD Card ROM Browser**: Load `.p8.png` or `.p8` cartridges directly from the SD card.
  - SDカードから `.p8.png` や `.p8` 形式のカートリッジを直接ロード可能。
- **Audio Support**: Enhanced audio synthesis for authentic PICO-8 SFX and Music playback.
  - PICO-8特有の効果音（SFX）やBGMを再現するオーディオエンジンを搭載。

## 🎮 Controls / 操作方法

- **Touch Screen / タッチスクリーン**: A virtual gamepad is displayed on the screen. (画面上に仮想コントローラーが表示されます)
  - **D-Pad / 方向キー**: Left side of the screen (画面左側)
  - **Button O**: Top right (画面右上). Hold for 1 second to lock the button state. (1秒長押しでボタンが押された状態にロックされます)
  - **Button X**: Bottom right (画面右下). Hold for 1 second to lock the button state. (1秒長押しでボタンが押された状態にロックされます)
  - **Start / Pause**: Bottom center (画面下部中央)
  - **Volume**: Left edge volume buttons (画面左端のボリュームボタン)

## ⚙️ Installation / インストール

### Via [M5Burner](https://docs.m5stack.com/en/uiflow/m5burner/intro) (Recommended)
You can easily install RONTO8 using [M5Burner](https://docs.m5stack.com/en/uiflow/m5burner/intro) with the following share code:
- **Share Code**: `dMCJ0QAV7oI00aqr`

[M5Burner](https://docs.m5stack.com/en/uiflow/m5burner/intro)のシェアコード検索から簡単にインストールできます：
- **シェアコード**: `dMCJ0QAV7oI00aqr`

### Building from Source / ソースからビルドする場合

1. Clone the repository:
   ```bash
   git clone https://github.com/Layer812/R8CoreS3.git
   cd R8CoreS3
   ```
2. Build and upload using PlatformIO:
   ```bash
   pio run --target upload ; pio device monitor
   ```
3. Prepare the SD Card:
   Place your `.p8.png` or `.p8` files on the root or in a folder on your MicroSD card and insert it into the CoreS3.
   - MicroSDカードのルートやフォルダ内に `.p8.png` または `.p8` ファイルを置き、CoreS3に挿入してください。

## 🙏 Credits and Acknowledgments / 謝辞

RONTO8 is heavily based on the incredible work of the open-source community:
- [benbaker76](https://github.com/benbaker76) - Original author and maintainer of [femto8](https://github.com/benbaker76/femto8)
- [Jacopo Santoni](https://github.com/Jakz) - Author of [retro8](https://github.com/Jakz/retro8)
- [Lexaloffle](https://www.lexaloffle.com/) - The visionary creator of the amazing PICO-8 fantasy console.
- **Layer8** - M5Stack CoreS3 Port, Audio engine fixes, memory optimization, and virtual gamepad implementation.

*(C) 2026 Layer8. BASED ON FEMTO8 & ZEPTO8.*



## 🎮 Supported ROMs(or To Be Cecked ) / 対応(確認中)のロム一覧

| title | author | bbs_url | playable | sound |
|---|---|---|---|---|
| geleste | jae2 | [Link](https://www.lexaloffle.com/bbs/?tid=156888) | OK | OK |
| Just One Boss | bridgs | [Link](https://www.lexaloffle.com/bbs/?tid=30767) | OK | OK |
| celeste classic 2 | noel | [Link](https://www.lexaloffle.com/bbs/?tid=41282) | OK | OK |
| Birds With Guns (100 000th post!) | Yolwoocle | [Link](https://www.lexaloffle.com/bbs/?tid=45334) | OK | OK |
| Terra - A Terraria Demake | cubee | [Link](https://www.lexaloffle.com/bbs/?tid=44606) | NG |  |
| PICOHOT | pirx_vr | [Link](https://www.lexaloffle.com/bbs/?tid=37236) | NG(download disable :) |  |
| High Stakes | Krystman | [Link](https://www.lexaloffle.com/bbs/?tid=40099) | OK | NG(no music) |
| PICO-BALL: a Sports Game Starring Jelpi | Munchkin | [Link](https://www.lexaloffle.com/bbs/?tid=150620) | NG(too heavy) | OK |
| Oblivion Eve | SmartAlloc | [Link](https://www.lexaloffle.com/bbs/?tid=140564) | NG(memory error) |  |
| Buns: Bunny survivor | unikotoast | [Link](https://www.lexaloffle.com/bbs/?tid=48032) | YES | NG(lack sound) |
| Marble Merger | 2darray | [Link](https://www.lexaloffle.com/bbs/?tid=54837) | OK | OK |
| PICOWARE | szczm_ | [Link](https://www.lexaloffle.com/bbs/?tid=34751) | NG(no picoware) |  |
| Porklike | Krystman | [Link](https://www.lexaloffle.com/bbs/?tid=37045) | OK | NG(lack sound) |
| FUZ | Jusiv | [Link](https://www.lexaloffle.com/bbs/?tid=34188) | OK | OK |
| ISLANDER - Idle Crafting Game | CarsonK | [Link](https://www.lexaloffle.com/bbs/?tid=39471) | OK(slow) | ON |
| Pico Zombie Garden: A PvZ Demake | FlyingSmog | [Link](https://www.lexaloffle.com/bbs/?tid=42252) | NG(mouse needed) |  |
| Kalikan | LouieChapm | [Link](https://www.lexaloffle.com/bbs/?tid=53315) | NG |  |
| Combo Pool | NuSan | [Link](https://www.lexaloffle.com/bbs/?tid=3467) | OK | NG(lack sound) |
| Pico World Race 1.2 | PAK9 | [Link](https://www.lexaloffle.com/bbs/?tid=46495) | NG |  |
| BAS | yokozuna | [Link](https://www.lexaloffle.com/bbs/?tid=54986) | OK | OK |
| Low Knight | krajzeg | [Link](https://www.lexaloffle.com/bbs/?tid=37055) | NG |  |
| Mistigri | benjamin_soule | [Link](https://www.lexaloffle.com/bbs/?tid=3421) | NG>OK | OK |
| Into Ruins | ericb | [Link](https://www.lexaloffle.com/bbs/?tid=49928) | NG |  |
| Beckon the Hellspawn | LokiStriker | [Link](https://www.lexaloffle.com/bbs/?tid=51555) | OK | OK |
| Woodworm | spratt | [Link](https://www.lexaloffle.com/bbs/?tid=142717) | OK | OK(Noise) |
| Pico Dino - Chrome's T-rex game reimagined | Yolwoocle | [Link](https://www.lexaloffle.com/bbs/?tid=40759) | NG(continue) | OK |
| PRAXIS FIGHTER X | ericb | [Link](https://www.lexaloffle.com/bbs/?tid=140077) | NG(heavy) |  |
| UFO Swamp Odyssey | paranoidcactus | [Link](https://www.lexaloffle.com/bbs/?tid=38153) | NG |  |
| Air Delivery | pianoman373 | [Link](https://www.lexaloffle.com/bbs/?tid=52598) | NG |  |
| Shape of Mind | Krystman | [Link](https://www.lexaloffle.com/bbs/?tid=50432) | OK | OK |
| Build a Jetpack | Aymeri | [Link](https://www.lexaloffle.com/bbs/?tid=144726) | OK | OK |
| Pico Fox | electricgryphon | [Link](https://www.lexaloffle.com/bbs/?tid=28067) | OK(slow) | OK |
| Low Mem Sky | Liquidream | [Link](https://www.lexaloffle.com/bbs/?tid=32724) | OK | OK |
| Alpine Alpaca | johanp | [Link](https://www.lexaloffle.com/bbs/?tid=32304) | OK | OK |
| WitchCraft TD | unikotoast | [Link](https://www.lexaloffle.com/bbs/?tid=49166) | OK | OK |
| From Rust To Ash | LokiStriker | [Link](https://www.lexaloffle.com/bbs/?tid=151194) | NG |  |
| Cattle Crisis | Krystman | [Link](https://www.lexaloffle.com/bbs/?tid=148741) | NG |  |
| Pico Checkmate | Krystman | [Link](https://www.lexaloffle.com/bbs/?tid=31213) | OK | OK |
| Sonic 2.5 SAGE 2020 | BoneVolt | [Link](https://www.lexaloffle.com/bbs/?tid=39517) | TBC |  |
| MOTION笳蹴EC | shoma | [Link](https://www.lexaloffle.com/bbs/?tid=53392) | TBC |  |
| Trial of the Sorcerer | Mot | [Link](https://www.lexaloffle.com/bbs/?tid=43833) | TBC |  |
| P.Craft | NuSan | [Link](https://www.lexaloffle.com/bbs/?tid=3200) | TBC |  |
| Hug Arena | benjamin_soule | [Link](https://www.lexaloffle.com/bbs/?tid=1813) | TBC |  |
| Moss Moss | nc0 | [Link](https://www.lexaloffle.com/bbs/?tid=155371) | TBC |  |
| Rolly | Davbo | [Link](https://www.lexaloffle.com/bbs/?tid=31526) | TBC |  |
| DrawApp v0.3 | SaKo | [Link](https://www.lexaloffle.com/bbs/?tid=55608) | TBC |  |
| Driftmania | MaxBize | [Link](https://www.lexaloffle.com/bbs/?tid=140202) | TBC |  |
| pigments | benjamin_soule | [Link](https://www.lexaloffle.com/bbs/?tid=40490) | TBC |  |
| X-Wing vs. Tie Fighter: Attack on the Deathstar | freds72 | [Link](https://www.lexaloffle.com/bbs/?tid=31443) | TBC |  |
| NUMBAKO | carasohmi | [Link](https://www.lexaloffle.com/bbs/?tid=155791) | TBC |  |
| Dinky Kong | Heracleum | [Link](https://www.lexaloffle.com/bbs/?tid=51877) | TBC |  |
| Pico-8 Tunes Volume 1 | Gruber | [Link](https://www.lexaloffle.com/bbs/?tid=29008) | TBC |  |
| Cab Ride | Powersaurus | [Link](https://www.lexaloffle.com/bbs/?tid=41332) | TBC |  |
| Tetyis (Tetris clone) | Spaz48 | [Link](https://www.lexaloffle.com/bbs/?tid=36749) | TBC |  |
| Linecraft - A Minecraft demake | El_Nicovw321 | [Link](https://www.lexaloffle.com/bbs/?tid=141064) | TBC |  |
| Wobblepaint | zep | [Link](https://www.lexaloffle.com/bbs/?tid=40058) | TBC |  |
| Puzzles of the Paladin | NerdyTeachers | [Link](https://www.lexaloffle.com/bbs/?tid=140488) | TBC |  |
| Night Ride | vladcom | [Link](https://www.lexaloffle.com/bbs/?tid=39650) | TBC |  |
| Snekburd - A Snakebird Demake? | Werxzy | [Link](https://www.lexaloffle.com/bbs/?tid=148665) | TBC |  |
| picokaiju | spoike | [Link](https://www.lexaloffle.com/bbs/?tid=45889) | TBC |  |
| Villager | partnano | [Link](https://www.lexaloffle.com/bbs/?tid=38905) | TBC |  |
| R-Type | TheRoboZ | [Link](https://www.lexaloffle.com/bbs/?tid=44842) | TBC |  |
| DUNGEON! | deklaswas | [Link](https://www.lexaloffle.com/bbs/?tid=46206) | TBC |  |
| Lab Cat | ooooggll | [Link](https://www.lexaloffle.com/bbs/?tid=54754) | TBC |  |
| Super Disc Box | Farbs | [Link](https://www.lexaloffle.com/bbs/?tid=40111) | TBC |  |
| Hot Wax | lorandbehold | [Link](https://www.lexaloffle.com/bbs/?tid=141756) | TBC |  |
| SPHONGOS | mkoloch | [Link](https://www.lexaloffle.com/bbs/?tid=147126) | TBC |  |
| The Heavens | reskob | [Link](https://www.lexaloffle.com/bbs/?tid=53946) | TBC |  |
| SlipWays | krajzeg | [Link](https://www.lexaloffle.com/bbs/?tid=30978) | TBC |  |
| Feed The Ducks | kittenm4ster | [Link](https://www.lexaloffle.com/bbs/?tid=29353) | TBC |  |
| Balloon | Lobo | [Link](https://www.lexaloffle.com/bbs/?tid=142641) | TBC |  |
| Crowded Dungeon Crawler | BoneVolt | [Link](https://www.lexaloffle.com/bbs/?tid=39865) | TBC |  |
| MetroCUBEvania | FlytrapStudios | [Link](https://www.lexaloffle.com/bbs/?tid=30643) | TBC |  |
| Wolfenstein 3D | hungrybutterfly | [Link](https://www.lexaloffle.com/bbs/?tid=28423) | TBC |  |
| Terra Nova Pinball v1.1.0 | xietanu | [Link](https://www.lexaloffle.com/bbs/?tid=49068) | TBC |  |
| HIT8OX | vladcom | [Link](https://www.lexaloffle.com/bbs/?tid=52278) | TBC |  |
| Super Mario Bros. | Sascha217 | [Link](https://www.lexaloffle.com/bbs/?tid=28942) | TBC |  |
| UnDUNE II - The Demaking of a Dynasty | Liquidream | [Link](https://www.lexaloffle.com/bbs/?tid=47155) | TBC |  |
| Pico de Pon | stevelavietes | [Link](https://www.lexaloffle.com/bbs/?tid=37280) | TBC |  |
| Pullfrog | arlefreak | [Link](https://www.lexaloffle.com/bbs/?tid=38636) | TBC |  |
| underworld siege (LD33) | benjamin_soule | [Link](https://www.lexaloffle.com/bbs/?tid=2319) | TBC |  |
| Pico-8 Tunes Vol. 2 | Gruber | [Link](https://www.lexaloffle.com/bbs/?tid=33675) | TBC |  |
| GRiPPY | ivy | [Link](https://www.lexaloffle.com/bbs/?tid=41299) | TBC |  |
| Alpine Ascent | TylerRDavis | [Link](https://www.lexaloffle.com/bbs/?tid=40791) | TBC |  |
| Feathered Escape | yokozuna | [Link](https://www.lexaloffle.com/bbs/?tid=140353) | TBC |  |
| Winterwood | Jusiv | [Link](https://www.lexaloffle.com/bbs/?tid=40927) | TBC |  |
| Downward | MisterWizard01 | [Link](https://www.lexaloffle.com/bbs/?tid=50042) | TBC |  |
| Little Eidolons | SmartAlloc | [Link](https://www.lexaloffle.com/bbs/?tid=51160) | TBC |  |
| Pico Off Road | assemblerbot | [Link](https://www.lexaloffle.com/bbs/?tid=41897) | TBC |  |
| floodedcaves | NuSan | [Link](https://www.lexaloffle.com/bbs/?tid=47247) | TBC |  |
| Steps | amidos2006 | [Link](https://www.lexaloffle.com/bbs/?tid=41005) | TBC |  |
| Bubble Bobble | pahammond | [Link](https://www.lexaloffle.com/bbs/?tid=37748) | TBC |  |
| Sneaky Stealy 1.4 | ironchestgames | [Link](https://www.lexaloffle.com/bbs/?tid=41185) | TBC |  |
| Solitomb | krajzeg | [Link](https://www.lexaloffle.com/bbs/?tid=145483) | TBC |  |
| HAKAI | Hobhob | [Link](https://www.lexaloffle.com/bbs/?tid=48971) | TBC |  |
| snﾃ､kﾃ､tor | rawArgon | [Link](https://www.lexaloffle.com/bbs/?tid=45316) | TBC |  |
| Quest for the Book of Truth | Mush | [Link](https://www.lexaloffle.com/bbs/?tid=32727) | TBC |  |
| SUPER World of Goo! | primary/convergence | [Link](https://www.lexaloffle.com/bbs/?tid=142393) | TBC |  |
| Beam | rupees | [Link](https://www.lexaloffle.com/bbs/?tid=142844) | TBC |  |
| Outvain | Ruva | [Link](https://www.lexaloffle.com/bbs/?tid=53571) | TBC |  |
| Cryomancer | suezou | [Link](https://www.lexaloffle.com/bbs/?tid=146410) | TBC |  |
| Super Hat Girl! | benvium | [Link](https://www.lexaloffle.com/bbs/?tid=148060) | TBC |  |
| Tomb of G'Nir | paranoidcactus | [Link](https://www.lexaloffle.com/bbs/?tid=31258) | TBC |  |
| Top Speed! | benvium | [Link](https://www.lexaloffle.com/bbs/?tid=148203) | TBC |  |
| Jack of Spades | BoneVolt | [Link](https://www.lexaloffle.com/bbs/?tid=34544) | TBC |  |
| Tiny Fisher | 2darray | [Link](https://www.lexaloffle.com/bbs/?tid=31129) | TBC |  |
| Ramps | Mot | [Link](https://www.lexaloffle.com/bbs/?tid=38221) | TBC |  |
| Palette-Maker | 2darray | [Link](https://www.lexaloffle.com/bbs/?tid=35462) | TBC |  |
| Witch n' Wiz | mhughson | [Link](https://www.lexaloffle.com/bbs/?tid=28944) | TBC |  |
| trichromat | spratt | [Link](https://www.lexaloffle.com/bbs/?tid=140628) | TBC |  |
| Dino Sort v1.1 | Adam Atomic | [Link](https://www.lexaloffle.com/bbs/?tid=149747) | TBC |  |
| The Carpathian | Trog | [Link](https://www.lexaloffle.com/bbs/?tid=48943) | TBC |  |
| Time For Lunch | zep | [Link](https://www.lexaloffle.com/bbs/?tid=52576) | TBC |  |
| Hydra | scgrn | [Link](https://www.lexaloffle.com/bbs/?tid=3191) | TBC |  |
| Slimey, Jump! | CarlosPedroso | [Link](https://www.lexaloffle.com/bbs/?tid=34532) | TBC |  |
| Dominion Ex | extar | [Link](https://www.lexaloffle.com/bbs/?tid=36865) | TBC |  |
| Pico Lake | Pavilion | [Link](https://www.lexaloffle.com/bbs/?tid=43659) | TBC |  |
| Redash | SmellyFishstiks | [Link](https://www.lexaloffle.com/bbs/?tid=54621) | TBC |  |
| Province | Perfoon | [Link](https://www.lexaloffle.com/bbs/?tid=148950) | TBC |  |
| Impossible Mission R.T. | carlc27843 | [Link](https://www.lexaloffle.com/bbs/?tid=41991) | TBC |  |
| Bubblegum Spin | Bee_Randon | [Link](https://www.lexaloffle.com/bbs/?tid=140885) | TBC |  |
| Samurise | Troypicol | [Link](https://www.lexaloffle.com/bbs/?tid=145581) | TBC |  |
| Mouse Required | Werxzy | [Link](https://www.lexaloffle.com/bbs/?tid=151030) | TBC |  |
| PERISHER, a CELESTE mod | managore | [Link](https://www.lexaloffle.com/bbs/?tid=27694) | TBC |  |
| Baba Is You Demake | ooooggll | [Link](https://www.lexaloffle.com/bbs/?tid=142638) | TBC |  |
| Ghost House | kittenm4ster | [Link](https://www.lexaloffle.com/bbs/?tid=35492) | TBC |  |
| Peral | Phvli | [Link](https://www.lexaloffle.com/bbs/?tid=43156) | TBC |  |
| Astropocalypse | picoter8 | [Link](https://www.lexaloffle.com/bbs/?tid=44713) | TBC |  |
| Super Mario Bros. (Authentic) | mhughson | [Link](https://www.lexaloffle.com/bbs/?tid=31744) | TBC |  |
| Neath | binaryeye | [Link](https://www.lexaloffle.com/bbs/?tid=147270) | TBC |  |
| NEMO - Puzzle Pack II | mooon | [Link](https://www.lexaloffle.com/bbs/?tid=47318) | TBC |  |
| Gemstone Dredging 1.9 | thesailor | [Link](https://www.lexaloffle.com/bbs/?tid=143676) | TBC |  |
| chrysopoeia | benjamin_soule | [Link](https://www.lexaloffle.com/bbs/?tid=29980) | TBC |  |
| Carrot Kingdom! | shoma | [Link](https://www.lexaloffle.com/bbs/?tid=156882) | TBC |  |
| Super Mario Bros. | JadeLombax | [Link](https://www.lexaloffle.com/bbs/?tid=145191) | TBC |  |
| Little Necromancer | Fred_Osterero | [Link](https://www.lexaloffle.com/bbs/?tid=37321) | TBC |  |
| Don't Dig Up the Dead | morningtoast | [Link](https://www.lexaloffle.com/bbs/?tid=51434) | TBC |  |
| Messages | Jusiv | [Link](https://www.lexaloffle.com/bbs/?tid=29501) | TBC |  |
| NULL | Jusiv | [Link](https://www.lexaloffle.com/bbs/?tid=30504) | TBC |  |
| Dragondot 3 (v1.1.1) | NMcCoy | [Link](https://www.lexaloffle.com/bbs/?tid=36789) | TBC |  |
| Wizardish - A First-Person Grid-Based Dungeon Crawler! v2.1 tiny update | Eduardolicious | [Link](https://www.lexaloffle.com/bbs/?tid=3585) | TBC |  |
| 512px under | pck404 | [Link](https://www.lexaloffle.com/bbs/?tid=45674) | TBC |  |
| Pirate's Trial | benjamin_soule | [Link](https://www.lexaloffle.com/bbs/?tid=39655) | TBC |  |
| Pony 9000 | arrowonionbelly | [Link](https://www.lexaloffle.com/bbs/?tid=140058) | TBC |  |
| Air Pico | Mot | [Link](https://www.lexaloffle.com/bbs/?tid=149301) | TBC |  |
| Valdi: Shadows | beepyeah | [Link](https://www.lexaloffle.com/bbs/?tid=31664) | TBC |  |
| Digger | paranoidcactus | [Link](https://www.lexaloffle.com/bbs/?tid=43338) | TBC |  |
| Walker | buzzard1337 | [Link](https://www.lexaloffle.com/bbs/?tid=149366) | TBC |  |
| We Missed You! (LD33 compo) | Rhys | [Link](https://www.lexaloffle.com/bbs/?tid=2322) | TBC |  |
| Frog King | pixpnd | [Link](https://www.lexaloffle.com/bbs/?tid=31417) | TBC |  |
| Bustin' 2.1 | morningtoast | [Link](https://www.lexaloffle.com/bbs/?tid=29948) | TBC |  |
| Spider-Bat: Horticultural Hero | kittenm4ster | [Link](https://www.lexaloffle.com/bbs/?tid=41062) | TBC |  |
| Skeleton Gelatin | Adam Atomic | [Link](https://www.lexaloffle.com/bbs/?tid=147116) | TBC |  |
| Libryinth | Munro | [Link](https://www.lexaloffle.com/bbs/?tid=151231) | TBC |  |
| 東方運命の星(Touhou Unmei no Hoshi) | Chronocide | [Link](https://www.lexaloffle.com/bbs/?tid=36992) | YES | OK |
| Dust Bunny | Adam Atomic | [Link](https://www.lexaloffle.com/bbs/?tid=148825) | TBC |  |
| mer ork | rawArgon | [Link](https://www.lexaloffle.com/bbs/?tid=143486) | TBC |  |
| Fighter Street II | BitwiseCreative | [Link](https://www.lexaloffle.com/bbs/?tid=40044) | TBC |  |
| The Wee Dungeon 1.2.3 | Parlor | [Link](https://www.lexaloffle.com/bbs/?tid=3072) | TBC |  |
| The Slow and the Curious | emu | [Link](https://www.lexaloffle.com/bbs/?tid=3597) | TBC |  |
| Heliopause | anthonysavatar | [Link](https://www.lexaloffle.com/bbs/?tid=28203) | TBC |  |
| Zip Zapper | ejreyes | [Link](https://www.lexaloffle.com/bbs/?tid=53344) | TBC |  |
| Pole Station | Pavilion | [Link](https://www.lexaloffle.com/bbs/?tid=147858) | TBC |  |
| Comanche 1/2 | electricgryphon | [Link](https://www.lexaloffle.com/bbs/?tid=31244) | TBC |  |
| To take root Among the Stars | somin | [Link](https://www.lexaloffle.com/bbs/?tid=51279) | TBC |  |
| Teenage Mutant Ninja Turtles in: Shredder's Prevenge APRIL UPDATE | Wolfe3D | [Link](https://www.lexaloffle.com/bbs/?tid=48795) | TBC |  |
| clockwise_knight | benjamin_soule | [Link](https://www.lexaloffle.com/bbs/?tid=151986) | TBC |  |