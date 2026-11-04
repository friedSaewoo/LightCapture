# 빌드 도구와 런타임 고지

LightCapture의 앱 소스와 아이콘은 이번 작업에서 작성했습니다. 알캡처 또는 ShareX의 소스·아이콘·이미지를 복사해 포함하지 않았습니다.

- 빌드 도구: w64devkit 2.10.1, GCC 16.2.0, GNU Binutils. 도구 자체는 배포 패키지에 포함하지 않습니다. [공식 저장소](https://github.com/skeeto/w64devkit)
- 실행 파일에는 MinGW-w64 런타임과 GCC C++ 런타임 일부가 정적으로 연결됩니다. MinGW-w64 고지 원문은 `licenses/COPYING.MinGW-w64-runtime.txt`에 포함합니다.
- GCC 런타임 관련 원문: [GCC Runtime Library Exception](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html), [GCC 라이선스 안내](https://gcc.gnu.org/onlinedocs/libstdc++/manual/appendix_free.html).
- Windows의 GDI+, Common Controls, GIF 인코딩·미리보기용 Windows Imaging Component(WIC) 등 시스템 구성요소를 사용합니다. 해당 시스템 DLL을 패키지에 재배포하지 않습니다. GIF 검증에 사용한 Pillow는 배포 실행 파일과 패키지에 포함하지 않습니다.

실행 파일에는 코드 서명을 적용하지 않았습니다. 현재 패키지는 Windows x64 대상으로 빌드한 개발 검증판입니다.
