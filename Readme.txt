tsreadex - MPEG-TSのストリーム選択と安定化のためのツール

使用法:

tsreadex [-z ignored][-s seek][-l limit][-t timeout][-m mode][-x pids][-n prog_num_or_index][-a aud1][-b aud2][-c cap][-u sup][-d flags] src

-z ignored
  必ず無視されるパラメータ(プロセス識別用など)。

-s seek (bytes), default=0
  ファイルの初期シーク量。0未満のときはファイル末尾から-(seek+1)だけ前方にシークする。
  入力がパイプ系のときは0でなければならない。

-l limit (kbytes/second), 0<=range<=32768, default=0
  入力の最大読み込み速度。0のとき無制限。
  "-n"オプションでサービスID指定する場合などで、もしそのサービスが見つからない場合には出力するものがないためストレージの
  最大負荷で読み込みが行われてしまうことになるが、このオプションで制限できる。

-t timeout (seconds), 0<=range<=600, default=0
  この秒数以上のあいだ出力が全くないときタイムアウトとして終了する。

-m mode, range=0 or 1 or 2, default=0
  タイムアウトの方式。
  0: 通常読み込み。
     timeoutが0のときは入力終了(ファイル終端など)までタイムアウトせず、入力終了後すぐに終了する。
     timeoutが0でないときは、ファイル終端に達した後ファイルに追記がないか待ってから終了する。
     入力がパイプ系のときはtimeout=0でなければならない。
  1: 容量確保ファイルの読み込み。
     mode=0のときと基本的に同じだが、ファイル終端または内容が正しいMPEG-TSでなくなった部分を終端とする。
     入力はパイプ系であってはならない。
  2: 非ブロッキングパイプ読み込み。
     入力終了後すぐに終了する。入力が滞った場合にも(タイムアウトにより)終了する。
     入力はパイプ系でなければならない。timeoutは0であってはならない。

-x pids, default=""
  取りのぞくTSパケットのPIDを'/'区切りで指定。

-n prog_num_or_index, -256<=range<=65535, default=0
  特定サービスのみを選択して出力するフィルタを有効にする。
  サービスID(1以上)かPAT(Program Association Table)上の並び順(先頭を-1として-1,-2,..)を指定する。
  PIDが0x0030未満か以下の特定ストリームのみ出力されるようになり、PIDは以下のように固定される。
  - 映像: PID=0x0100
  - 第1音声(AACのみ): PID=0x0110
  - 第2音声(AACのみ): PID=0x0111
  - ARIB字幕: PID=0x0130
  - ARIB文字スーパー: PID=0x0138
  - PMT(Program Map Table): PID=0x01f0
  - PCR(Program Clock Reference): PID=0x01ff (ただし上記ストリームに重畳されている場合はそのPID)
  このフィルタが有効でないとき"-a"、"-b"、"-c"、"-u"オプションは無視される。

-a aud1, range=0 or 1 [+4] [+8], default=0
  第1音声をそのままか、補完するか。
  1のとき、ストリームが存在しなければPMTの項目を補って無音のAACストリームを挿入する。
  +4のとき、モノラルであればステレオにする。
  +8のとき、デュアルモノ(ARIB STD-B32)を2つのモノラル音声に分離し、右音声を第2音声として扱う。

-b aud2, range=0 or 1 or 2 [+4], default=0
  第2音声をそのままか、補完するか、削除するか。
  1のとき、ストリームが存在しなければPMTの項目を補って無音のAACストリームを挿入する。
  +4のとき、モノラルであればステレオにする。

-c cap, range=0 or 1 or 2, default=0
  ARIB字幕をそのままか、補完するか、削除するか。
  1のとき、ストリームが存在しなければPMTの項目を補う。

-u sup, range=0 or 1 or 2, default=0
  ARIB文字スーパーをそのままか、補完するか、削除するか。
  1のとき、ストリームが存在しなければPMTの項目を補う。

-d flags, range=0 or 1 [+2] [+4] [+8], default=0
  ARIB字幕/文字スーパーを https://github.com/monyone/aribb24.js が解釈できるID3 timed-metadataに変換する。
  変換元のストリームは削除される。
  +2のとき、不明な"private data"ストリームをARIB文字スーパーとして扱う。ffmpegを経由した入力など記述子が正しく転送されて
  いない入力に対処するもので、普通は使わない。
  +4のとき、変換後のストリームに規格外の5バイトのデータを追加する。これはffmpeg 4.4時点のlibavformat/mpegts.cに存在する
  バグを打ち消すためのもので、node-arib-subtitle-timedmetadaterの手法に基づく。出力をffmpegなどに渡す場合にのみ使用する
  こと。
  +8のとき、変換後のストリームのPTS(Presentation Timestamp)が単調増加となるように調整する。ffmpeg 4.4時点においてPTSを
  DTS(Decoding Timestamp)とみなしタイムスタンプが遡るとエラーとなるのを防ぐもの。ARIB字幕/文字スーパーの両方が存在する場
  合で、出力をffmpegなどに渡す場合に使用する。

src
  入力ファイル名、または"-"で標準入力

説明:

このツールは大まかに3段のフィルター構造になっている。入力された188か192か204バイトのMPEG-TSパケットを同期語(0x47)で同期
し、最初に"-x"オプションで指定されたパケットを取りのぞく(1段)。つぎに、"-n"オプションが0でないときは特定サービスを選択し
てストリームの補完などを行う(2段)。最後に"-d"オプションによる変換を行い(3段)、188バイトのTSパケットとして標準出力する。
たとえば、ARIB仕様のTSパケットからEIT(番組表データ)を取り除き、PATの先頭サービスのみ出力し、第2音声と字幕がつねに存在す
るようにして文字スーパーを削除し、ARIB字幕をID3 timed-metadataに変換するときは以下のようになる。
> tsreadex -x 18/38/39 -n -1 -b 1 -c 1 -u 2 -d 1 src.m2t > dest.m2t

その他:

ライセンスはMITとする。

"-n"オプションはおもにffmpegが動的な副音声追加などを扱えないのをなんとかするためのもの。ffmpegなどの改善により不要になる
のが理想だと思う。

"-n"オプションはtsukumi( https://github.com/tsukumijima )氏のSlack上でのアイデアをもとに設計した。

"-d"オプションの実装にあたり https://github.com/monyone/node-arib-subtitle-timedmetadater を参考にした。

デュアルモノ分離などの実装にあたり https://github.com/monyone/node-aac-dualmono-splitter を参考にした。
デュアルモノ分離、ステレオ化は典型的な形式(AAC-LC,48/44.1/32kHz)のみ対応。ほかの形式に対しては原則、無変換になる(はず)。
