# padfix: ETS2 / ATS için sabit gamepad

Euro Truck Simulator 2 ve American Truck Simulator için bir SDK eklentisi. Xbox uyumlu (XInput) gamepad'ini oyuna **adı hiç değişmeyen tek bir cihaz** olarak verir: **"Sabit Gamepad"**. Pad kapanıp açılsa, başka USB'ye takılsa ya da Windows onu farklı tanısa da tuş atamaların kaybolmaz.

> **English:** An SCS SDK input plugin for ETS2/ATS that exposes your XInput gamepad as one stable device ("Sabit Gamepad", `sdk.padfix`). Bindings no longer break when the pad reconnects and gets a new DirectInput GUID, or when the game loses buttons through its own XInput/Steam Input handling. Download the zip from Releases and run `install.bat`.

## Hangi sorunu çözüyor?

ETS, gamepad'i aynı anda birkaç yoldan görüyor ve her birinin kendi sorunu var:

- **DirectInput:** Windows pad'e her bağlanışta farklı bir kimlik (GUID) verebiliyor. ETS atamaları bu kimliğe kaydettiği için pad kapanıp açılınca tuşlar gidiyor.
- **Steam Input:** Açıkken A/B/X gibi tuşları Steam yakalıyor ve oyunun XInput cihazına ulaşmıyorlar.
- **ETS'nin XInput cihazı:** Bazı kurulumlarda bazı tuşlar (A, B, X, LB, çubuk tıklamaları, yön tuşları) oyuna hiç ulaşmıyor; aynı tuşlar oyunun dışında sorunsuz okunuyor.

padfix bunların hiçbirine dayanmıyor. Pad'i oyunun **dışında** okuyor ve ETS'ye resmi SDK giriş API'si üzerinden veriyor.

## Nasıl çalışıyor?

```
gamepad --XInput--> padfix_helper.exe --paylaşılan bellek--> padfix.dll --SCS Input SDK--> ETS2/ATS
                    (oyunun dışında)                         (plugins klasöründe)
```

- `padfix.dll` oyun açılırken yüklenir, `padfix_helper.exe`'yi başlatır ve oyuna 30 girişli "Sabit Gamepad" cihazını tanıtır.
- `padfix_helper.exe` ilk bağlı XInput pad'i saniyede yaklaşık 400 kez okur. Pad kopup yeniden bağlanırsa onu kendisi bulur. Oyun kapanınca o da kapanır.
- ETS bazen Sabit Gamepad'i kendi "kara listesine" alıp kontroller menüsünde gizliyor. Yardımcı program **oyun kapandıktan sonra** `global_controls.sii` dosyasına bakar; cihaz kara listedeyse beyaz listeye taşır. Bir sonraki açılışta cihaz menüde görünür.

### Girişler

| Tür | Girişler |
|---|---|
| Eksenler | `lx`, `ly`, `rx`, `ry` (çubuklar, -1..1, yukarı/sağ pozitif), `lt`, `rt` (tetikler, 0..1) |
| Tuşlar | `a`, `b`, `x`, `y`, `lb`, `rb`, `back`, `start`, `ls`, `rs` (çubuk tıklamaları) |
| Yön tuşları | `dpad_up`, `dpad_down`, `dpad_left`, `dpad_right` |
| Tuş gibi tetik/çubuk | `lt_press`, `rt_press`, `lstick_up/down/left/right`, `rstick_up/down/left/right` |

`controls.sii` içinde örnek: ``device joy `sdk.padfix` `` ve ``mix horn `keyboard.h?0 | joy.a?0` ``.

## Kurulum

1. [Releases](../../releases) sayfasından `padfix-x.y.z.zip` dosyasını indir ve bir klasöre çıkar.
2. Oyunu kapat.
3. `install.bat`'ı çalıştır. Kurulum, oyunu Steam kütüphanelerinde kendisi bulur. Bulamazsa oyun klasörünü ver:
   ```
   install.bat -GameDir "D:\SteamLibrary\steamapps\common\Euro Truck Simulator 2"
   ```
4. Steam'de oyuna sağ tıkla → **Özellikler → Kontrolcü** → **Steam Input'u devre dışı bırak**.
5. Oyunu aç. Açılışta çıkan "gelişmiş SDK özellikleri" uyarısında devam et (eklenti kurulu olduğu sürece her açılışta çıkar).
6. **Seçenekler → Kontroller → Giriş türü: "Klavye + Sabit Gamepad"** seç, sonra **Tuşlar ve Butonlar**'dan tuşlarını ata.

Kurulum şu dosyaları ekler:
```
<oyun>\bin\win_x64\plugins\padfix.dll
<oyun>\bin\win_x64\plugins\padfix\padfix_helper.exe
```

## Kaldırma

Oyunu kapat ve `uninstall.bat`'ı çalıştır. Sonra kontrollerini oyunda tekrar kendi gamepad'ine ata.

## Sınırlamalar

- Sadece **XInput** pad'ler: Xbox pad'leri ve X-input modundaki 8BitDo gibi uyumlu pad'ler. PlayStation pad'leri doğrudan desteklenmiyor.
- Tek pad: ilk bağlı XInput pad kullanılır.
- Sadece Windows (64-bit).
- Çevrimiçi modlarla (ör. TruckersMP) uyumluluğu denenmedi.

## Mevcut atamaları taşımak

Atamaların ETS'nin XInput veya DirectInput cihazındaysa, `tools/convert_to_padfix.py` hepsini aynı tuşlarla Sabit Gamepad'e taşır (oyun kapalıyken):

```
python tools/convert_to_padfix.py "<Belgeler>\Euro Truck Simulator 2\steam_profiles\<profil>\controls.sii"          (sadece gösterir)
python tools/convert_to_padfix.py "<Belgeler>\Euro Truck Simulator 2\steam_profiles\<profil>\controls.sii" --write  (yazar)
```

Önce dosyanın yedeğini al.

## Derleme

Gerekenler: Visual Studio 2022 veya üstü ("C++ ile masaüstü geliştirme").

```
build.bat                    build\padfix.dll ve build\padfix_helper.exe
powershell -File make_release.ps1 -Version 1.0.0     dist\padfix-1.0.0.zip
```

Geliştirme araçları (`tools/`):
- `host_test.cpp`: DLL'i oyun gibi yükler, cihazı kaydeder ve olayları çeker.
- `check_helper.py`: yardımcı programı tek başına çalıştırıp paylaşılan belleği okur.

## Lisans

MIT, bkz. [LICENSE](LICENSE). `sdk/` altındaki SCS SDK başlık dosyaları SCS Software'e aittir ve [kendi lisanslarıyla](sdk/sdk_license.txt) dağıtılır.
