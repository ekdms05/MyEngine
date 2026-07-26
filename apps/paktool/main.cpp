// paktool — .pak 아카이브 패커/리스터 CLI (docs/04 배포).
//
//   paktool pack <inputDir> <output.pak>   : 디렉터리 트리를 .pak 으로 묶는다.
//   paktool list <archive.pak>             : .pak 항목을 나열한다.
//
// 배포 파이프라인: 게임 assets/ 디렉터리를 pack 해 단일 .pak 으로 배포하고, 런타임은
// PakFileSystem 으로 마운트한다(loose 파일 없이 스탠드얼론 실행).
#include "mye/asset/PakFile.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace mye;

namespace {

int DoPack(const std::string& inDir, const std::string& outPak) {
    asset::PakWriter w;
    auto added = w.AddDirectory(inDir);
    if (!added) {
        std::fprintf(stderr, "pack 실패: %s\n", added.GetError().message.c_str());
        return 1;
    }
    auto wrote = w.Write(outPak);
    if (!wrote) {
        std::fprintf(stderr, "쓰기 실패: %s\n", wrote.GetError().message.c_str());
        return 1;
    }
    std::printf("packed %d files -> %s\n", added.Value(), outPak.c_str());
    return 0;
}

int DoList(const std::string& pak) {
    auto fsRes = asset::PakFileSystem::Open(pak);
    if (!fsRes) {
        std::fprintf(stderr, "열기 실패: %s\n", fsRes.GetError().message.c_str());
        return 1;
    }
    auto& fs = *fsRes.Value();
    int n = 0;
    fs.Enumerate("", [&](std::string_view p) {
        std::printf("  %.*s\n", static_cast<int>(p.size()), p.data());
        ++n;
    });
    std::printf("%d entries in %s\n", n, pak.c_str());
    return 0;
}

void Usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  paktool pack <inputDir> <output.pak>\n"
                 "  paktool list <archive.pak>\n");
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() >= 3 && args[0] == "pack") return DoPack(args[1], args[2]);
    if (args.size() >= 2 && args[0] == "list") return DoList(args[1]);
    Usage();
    return 2;
}
