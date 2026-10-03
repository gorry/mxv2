// mxv2 - macOS のリソースパスとネイティブフォルダ選択
#import <AppKit/AppKit.h>

#include "fileutil.h"
#include "macosfileutil.h"

namespace mxv2 {

std::string MacResourceDir() {
	@autoreleasepool {
		NSBundle *bundle = [NSBundle mainBundle];
		// CLI の検証ツールでは実行ファイルの隣。Finder 起動は Resources。
		NSString *path = [[bundle.bundlePath pathExtension] isEqualToString:@"app"]
		                     ? bundle.resourcePath : [bundle.executablePath stringByDeletingLastPathComponent];
		return path ? std::string(path.fileSystemRepresentation) : std::string("./");
	}
}

std::string MacExecutableBaseName() {
	@autoreleasepool {
		NSString *path = [[NSBundle mainBundle].executablePath lastPathComponent];
		return path ? std::string(path.UTF8String) : std::string("mxv2");
	}
}

bool HasFolderBrowser() { return true; }

bool BrowseForFolder(const std::string &title, const std::string &start, void *owner,
                     std::string *out) {
	(void)owner;
	out->clear();
	@autoreleasepool {
		NSOpenPanel *panel = [NSOpenPanel openPanel];
		panel.canChooseDirectories = YES;
		panel.canChooseFiles = NO;
		panel.allowsMultipleSelection = NO;
		panel.canCreateDirectories = NO;
		panel.title = [NSString stringWithUTF8String:title.c_str()];
		if (!start.empty()) {
			NSString *path = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:start.c_str()
			                                                                          length:start.size()];
			panel.directoryURL = [NSURL fileURLWithPath:path isDirectory:YES];
		}
		if ([panel runModal] != NSModalResponseOK || panel.URL == nil) return false;
		*out = panel.URL.fileSystemRepresentation;
		return !out->empty();
	}
}

}  // namespace mxv2
