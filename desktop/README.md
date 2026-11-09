# LightCapture macOS·Linux 미리보기

LightCapture의 화면 캡처와 편집을 macOS와 Linux에서 사용할 수 있는 별도 빌드입니다. 배포 대상은 macOS 14 이상 Apple Silicon·Intel과 Ubuntu 24.04 x64입니다.

## 설치와 캡처

- **macOS**에서 ZIP을 풀고 `LightCapture.app`을 응용 프로그램 폴더에 넣습니다. 미리보기 빌드는 개발자 ID 서명과 Apple 공증을 사용하지 않아 시스템 정책에 따라 열기가 제한될 수 있습니다. 첫 캡처에서 시스템 설정 → 개인정보 보호 및 보안 → 화면 및 시스템 오디오 녹음의 LightCapture 권한을 확인합니다.
- **Ubuntu 24.04 x64**에서 `sudo apt install ./LightCapture-1.2.0-preview.1-ubuntu24.04-amd64.deb`를 실행합니다. 앱 메뉴의 LightCapture 또는 `lightcapture` 명령으로 엽니다.
- **macOS·X11**에서 **Ctrl+Shift+X** 또는 상단 **영역 캡처**를 누릅니다. 마우스가 있는 모니터에서 드래그하거나 **현재 화면**을 누릅니다. 전역 단축키는 상단 **단축키**에서 바꿀 수 있습니다.
- **Linux Wayland**에서 **영역 캡처**를 누르면 데스크톱의 캡처 선택창이 열립니다. 영역 선택 항목은 포털의 구현에 따라 달라집니다. 키보드 설정에서 **사용자 지정 단축키**에 `lightcapture --capture`를 연결할 수 있습니다.

## 편집과 GIF 저장

오른쪽 목록에서 사진을 클릭하면 편집 대상을 바꾸고 체크합니다. Shift+클릭으로 범위를 체크합니다. 목록의 **저장**과 **삭제**는 체크한 사진에 적용됩니다. **현재 저장**은 현재 사진 한 장을 PNG로 내보냅니다.

펜, 도형, 형광펜, 텍스트, 모자이크, 자르기와 사각형 선택을 사용할 수 있습니다. 선택 영역의 안쪽을 끌면 이동하고 가장자리에서 크기를 조절합니다. Delete는 선택 영역을 흰색으로 채웁니다. Ctrl+Z(macOS는 Command+Z)로 마지막 편집을 되돌립니다. 붙여넣은 이미지와 캔버스 여백은 **캔버스 크기**에서 조절할 수 있습니다. 캡처의 편집 결과는 별도 보관함에 PNG로 저장합니다. 실행 취소 기록은 현재 편집 중인 이미지에 유지합니다.

macOS와 X11에서 **GIF 녹화 → 영역 지정 → 화질 → 시작** 순으로 진행합니다. 3초 뒤 녹화를 시작하며 **정지**나 등록한 캡처 단축키로 미리보기를 엽니다. 기본은 12fps·긴 변 최대 1280px입니다. 녹화는 최대 2분 또는 약 80MiB에서 종료됩니다. Wayland에서는 사진 캡처와 편집을 지원합니다.

Windows판의 스크롤·창·자유형 캡처, 모니터를 가로지르는 영역, Windows 보관함 연동, 텍스트 상자의 이동·크기 조절은 이번 미리보기 범위에서 제외했습니다.

## 빌드와 검증

Ubuntu에서 `cmake ninja-build g++ qt6-base-dev libx11-dev libgif-dev`가 필요합니다.

```sh
cmake -S desktop -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-desktop
xvfb-run -a build-desktop/lightcapture --self-test
bash desktop/package-linux.sh build-desktop artifacts
```

macOS에서 Qt 6.8.3, giflib와 macOS 14 이상 SDK가 필요합니다. GitHub Actions는 두 CPU 종류의 앱을 각각 빌드하고 `macdeployqt`로 의존성을 묶습니다. 합성 이미지 시험은 편집 픽셀·보관함·GIF 프레임을 검증합니다. 실제 사용자 화면의 캡처 권한 승인, 물리 단축키와 Wayland 포털 선택창은 실기기 확인이 필요합니다.

이 빌드는 Qt Widgets를 동적 연결합니다. macOS 번들에 포함된 Qt 6.8.3의 [대응 소스](https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz)와 [라이선스 안내](https://doc.qt.io/qt-6/licensing.html)를 참고하세요. giflib는 MIT 라이선스이며 macOS 배포 ZIP에 고지를 포함합니다. Linux 패키지는 배포판의 Qt와 giflib를 사용합니다.
