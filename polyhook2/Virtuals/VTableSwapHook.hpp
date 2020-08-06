#ifndef POLYHOOK_2_0_VTBLSWAPHOOK_HPP
#define POLYHOOK_2_0_VTBLSWAPHOOK_HPP

#include <cassert>
#include <memory>
#include <map>

#include <unknwn.h>  // IUnknown

#include "polyhook2/IHook.hpp"
#include "polyhook2/MemProtector.hpp"
#include "polyhook2/Misc.hpp"

namespace PLH {

typedef std::map<uint16_t, uint64_t> VFuncMap;

// storage class for address of a virtual function
// also stores the function pointer type and index number on the class level
template<uint16_t I, typename FuncPtr>
struct VFunc {
	VFunc() : func(nullptr) {};
	VFunc(FuncPtr f) : func(f) {};
	const FuncPtr func;
	static const uint16_t func_index;
	typedef FuncPtr func_type;
};

// definition of constant must reside outside class declaration
template<uint16_t I, typename FuncPtr> const uint16_t VFunc<I, FuncPtr>::func_index = I;

class VTableSwapHook : public PLH::IHook {
public:
	VTableSwapHook(const uint64_t Class);
	VTableSwapHook(const uint64_t Class, const VFuncMap& redirectMap);
	VTableSwapHook(const char* Class, const VFuncMap& redirectMap);

	template<uint16_t I, typename FuncPtr, typename ... VFuncTypes>
	VTableSwapHook(const uint64_t Class, VFunc<I, FuncPtr> vfunc, VFuncTypes ... vfuncs)
		: VTableSwapHook(Class, vfuncs ...)
	{
		m_redirectMap[I] = reinterpret_cast<uint64_t>(vfunc.func);
	};

	virtual ~VTableSwapHook() = default;

	const VFuncMap& getOriginals() const;

	template<typename VFuncType, typename ... Args>
	auto origFunc(Args&& ... args) {
		auto func = reinterpret_cast<typename VFuncType::func_type>(m_origVFuncs.at(VFuncType::func_index));
		return func(std::forward<Args>(args) ...);
	};

	virtual bool hook() override;
	virtual bool unHook() override;
	virtual HookType getType() const override {
		return HookType::VTableSwap;
	}
private:
	uint16_t countVFuncs();

	std::unique_ptr<uintptr_t[]> m_newVtable;
	uintptr_t* m_origVtable;

	uint64_t  m_class;

	uint16_t  m_vFuncCount;

	// index -> ptr val 
	VFuncMap m_redirectMap;
	VFuncMap m_origVFuncs;
	bool m_Hooked;
};

namespace Helper {

// helper classes for VTableSwapHook with
// - RAII (automatically hook in constructor, unhook in destructor)
// - extra type check for origFunc
// - share ownership of instance, so it is not destroyed before hook is destroyed

// VTableSwapHook to an object managed by a shared_ptr (or unique_ptr via std::move)
template<typename T, typename ... VFuncTypes>
class SharedVTableSwapHook : private PLH::VTableSwapHook {
public:
	SharedVTableSwapHook(std::shared_ptr<T> instance, VFuncTypes ... new_funcs)
		: PLH::VTableSwapHook(reinterpret_cast<uint64_t>(instance.get()), new_funcs ...)
		, m_shared_ptr(instance)
	{
		static_assert(!std::is_base_of_v<IUnknown, T>, "Use PLH::Helper::ComVTableSwapHook for COM classes.");
		hook();
	};

	template<typename VFuncType, typename ... Args>
	inline auto origFunc(Args&& ... args) {
		static_assert(std::disjunction_v<std::is_same<VFuncType, VFuncTypes> ...>);
		return PLH::VTableSwapHook::origFunc<VFuncType>(std::forward<Args>(args) ...);
	};

	virtual ~SharedVTableSwapHook()
	{
		unHook();
	}

private:
	// shared pointer to maintain ownership
	std::shared_ptr<T> m_shared_ptr;
};

// VTableSwapHook to a COM object (anything deriving from IUnknown)
template<typename T, typename ... VFuncTypes>
class ComVTableSwapHook : private PLH::VTableSwapHook {
public:
	ComVTableSwapHook(T* instance, VFuncTypes ... new_funcs)
		: PLH::VTableSwapHook(reinterpret_cast<uint64_t>(instance), new_funcs ...)
		, m_com_ptr(instance)
	{
		static_assert(std::is_base_of_v<IUnknown, T>, "Use PLH::Helper::SharedVTableSwapHook for non COM classes.");
		m_com_ptr->AddRef();
		hook();
	};

	template<typename VFuncType, typename ... Args>
	inline auto origFunc(Args&& ... args) {
		static_assert(std::disjunction_v<std::is_same<VFuncType, VFuncTypes> ...>);
		return PLH::VTableSwapHook::origFunc<VFuncType>(std::forward<Args>(args) ...);
	};

	virtual ~ComVTableSwapHook()
	{
		unHook();
		m_com_ptr->Release();
	}

private:
	// com object pointer to get and release ownership
	T* m_com_ptr;
};

} // namespace Helper

} // namespace PLH

#endif