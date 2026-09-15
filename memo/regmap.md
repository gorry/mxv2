mxv2の鍵盤の長押しで、OPMレジスタ内容一覧を現在のビューの上にオーバーレイ表示します。
- 操作マッピングが鍵盤で、以下の表示座標も鍵盤の上ですが、鍵盤の描画とは特に関係ありません。
- オーバーレイ表示のON/OFFは保存しません。起動時は常にOFFです。
- layout.iniに[RegMap]を追加し、以下を定義します。
  - Rect=0,0,340,229        ; 矩形。BackColor/BackColorBrightで塗りつぶす。
  - Pos=1,1                 ; 文字を描き始める左上座標。Rectからの相対位置
- colors.iniに[RegMap]を追加し、以下を定義します。
  - Color=16777215          ; 文字の色
  - ColorBright=100         ; 文字の明るさ
  - BackColor=0             ; 背景の色
  - BackColorBright=50      ; 背景の明るさ
- OPMレジスタ内容一覧のレイアウトは以下の通りです。"FF"がレジスタ内容、右端の"AD"がレジスタ番号のベースです。
```
     |   LFO-RESET           |KEY                NFRQ|AD
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|00
     |CLKA  CLKB  TIMER  LFRQ|   P/AMD W/CT          |  
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|10
     |                       |                       |  
  CH.| 1  2  3  4  5  6  7  8|                       |  
AL/FB|FF FF FF FF FF FF FF FF|                       |20
   KC|FF FF FF FF FF FF FF FF|                       |28
   KF|FF FF FF FF FF FF FF FF|                       |30
P/AMS|FF FF FF FF FF FF FF FF|                       |38
     |                       |                       |  
     |       OP.1/OP.2       |        OP.3/OP.4      |  
  CH.| 1  2  3  4  5  6  7  8| 1  2  3  4  5  6  7  8|  
DT/ML|FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|40
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|50
   TL|FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|60
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|70
KS/AR|FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|80
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|90
AM/DR|FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|A0
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|B0
D2/SR|FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|C0
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|D0
SL/RR|FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|E0
     |FF FF FF FF FF FF FF FF|FF FF FF FF FF FF FF FF|F0
```



