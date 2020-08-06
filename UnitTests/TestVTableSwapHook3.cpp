#include <memory>
#include <stdexcept>

// for COM testing
#include <wrl/client.h>
#include <ShlObj.h>

#include <Catch.hpp>

#include "polyhook2/Virtuals/VTableSwapHook.hpp"
#include "polyhook2/Tests/StackCanary.hpp"
#include "polyhook2/Tests/TestEffectTracker.hpp"

EffectTracker vTblSwapEffects3;

// original class

class MyClass3 {
public:
	virtual ~MyClass3() {}

	virtual int method1(int x) {
		return 2 * x;
	}

	virtual int method2(int x, int y) {
		return x + y;
	}
};

class MyDerivedClass3 : public MyClass3 {
public:
	virtual int method3(int x, int y, int z) {
		return (x + y) * z;
	}
};

// virtual function hooks

int myclass3_method1(MyClass3* pThis, int x);
int myclass3_method2(MyClass3* pThis, int x, int y);

STDAPI IShellItem_GetDisplayName(IShellItem* pThis, SIGDN sigdnName, LPWSTR* ppszName);

// helper typedefs, and unique_ptr for storing the hook

typedef PLH::VFunc<1, decltype(&myclass3_method1)> VMethod1;
typedef PLH::VFunc<2, decltype(&myclass3_method2)> VMethod2;
typedef PLH::Helper::SharedVTableSwapHook<MyClass3, VMethod1, VMethod2> VTableMyClass;
std::unique_ptr<VTableMyClass> hook = nullptr;

typedef PLH::VFunc<5, decltype(&IShellItem_GetDisplayName)> VGetDisplayName;
typedef PLH::Helper::ComVTableSwapHook<IShellItem, VGetDisplayName> VTableIShellItem;
std::unique_ptr<VTableIShellItem> comhook = nullptr;

// hook implementations

NOINLINE int myclass3_method1(MyClass3* pThis, int x) {
	vTblSwapEffects3.PeakEffect().trigger();
	return hook->origFunc<VMethod1>(pThis, x) + 1;
}

NOINLINE int myclass3_method2(MyClass3* pThis, int x, int y) {
	vTblSwapEffects3.PeakEffect().trigger();
	return hook->origFunc<VMethod2>(pThis, x, y) + 2;
}

NOINLINE STDAPI IShellItem_GetDisplayName(IShellItem* pThis, SIGDN sigdnName, LPWSTR* ppszName) {
	vTblSwapEffects3.PeakEffect().trigger();
	auto hr = comhook->origFunc<VGetDisplayName>(pThis, sigdnName, ppszName);
	if (SUCCEEDED(hr)) {
		std::wcout << L"GetDisplayName: " << *ppszName << std::endl;
	}
	return hr;
}

// test case

TEST_CASE("VTableSwap3 tests", "[VTableSwap3]") {

	SECTION("Verify vtable shared_ptr helper") {
		PLH::StackCanary canary;
		auto ClassToHook = std::make_shared<MyDerivedClass3>();
		REQUIRE(ClassToHook->method1(3) == 6);
		REQUIRE(ClassToHook->method2(13, 9) == 22);
		REQUIRE(ClassToHook->method3(5, 11, 7) == 112);
		hook = std::make_unique<VTableMyClass>(
			ClassToHook,  // automatic cast to MyClass3
			VMethod1(&myclass3_method1),
			VMethod2(&myclass3_method2));

		vTblSwapEffects3.PushEffect();
		REQUIRE(ClassToHook->method1(3) == 7);
		REQUIRE(vTblSwapEffects3.PopEffect().didExecute());

		vTblSwapEffects3.PushEffect();
		REQUIRE(ClassToHook->method2(13, 9) == 24);
		REQUIRE(vTblSwapEffects3.PopEffect().didExecute());

		vTblSwapEffects3.PushEffect();
		REQUIRE(ClassToHook->method3(5, 11, 7) == 112);
		REQUIRE(!vTblSwapEffects3.PopEffect().didExecute());

		REQUIRE(ClassToHook.use_count() == 2);
		hook = nullptr;
		REQUIRE(ClassToHook.use_count() == 1);

		vTblSwapEffects3.PushEffect();
		REQUIRE(ClassToHook->method1(3) == 6);
		REQUIRE(!vTblSwapEffects3.PopEffect().didExecute());

		vTblSwapEffects3.PushEffect();
		REQUIRE(ClassToHook->method2(13, 9) == 22);
		REQUIRE(!vTblSwapEffects3.PopEffect().didExecute());

		vTblSwapEffects3.PushEffect();
		REQUIRE(ClassToHook->method3(5, 11, 7) == 112);
		REQUIRE(!vTblSwapEffects3.PopEffect().didExecute());
	}

	SECTION("Verify vtable COM helper") {
		PLH::StackCanary canary;
		REQUIRE(SUCCEEDED(CoInitialize(0)));
		PIDLIST_ABSOLUTE pidl;
		REQUIRE(SUCCEEDED(SHGetFolderLocation(0, CSIDL_DRIVES, 0, 0, &pidl)));
		Microsoft::WRL::ComPtr<IShellItem> pItem{ nullptr };
		REQUIRE(SUCCEEDED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(pItem.GetAddressOf()))));
		REQUIRE(pItem != nullptr);
		pItem->AddRef();
		REQUIRE(pItem->Release() == 1); // trick for getting refcount
		ILFree(pidl);
		comhook = std::make_unique<VTableIShellItem>(
			pItem.Get(),
			VGetDisplayName(&IShellItem_GetDisplayName));
		pItem->AddRef();
		REQUIRE(pItem->Release() == 2); // trick for getting refcount
		PWSTR szName = nullptr;
		vTblSwapEffects3.PushEffect();
		REQUIRE(SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY, &szName)));
		REQUIRE(vTblSwapEffects3.PopEffect().didExecute());
		CoTaskMemFree(szName);
		comhook = nullptr;
		pItem->AddRef();
		REQUIRE(pItem->Release() == 1); // trick for getting refcount
		vTblSwapEffects3.PushEffect();
		REQUIRE(SUCCEEDED(pItem->GetDisplayName(SIGDN_NORMALDISPLAY, &szName)));
		REQUIRE(!vTblSwapEffects3.PopEffect().didExecute());
		CoTaskMemFree(szName);		
		REQUIRE(pItem->Release() == 0);
		CoUninitialize();
	}
}