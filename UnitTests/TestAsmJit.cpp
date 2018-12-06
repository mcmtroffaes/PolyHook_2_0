#include <Catch.hpp>

#include "headers/Detour/ILCallback.hpp"

typedef int(*Func)(void);
TEST_CASE("Minimal Example", "[AsmJit]") {
	asmjit::JitRuntime rt;                          // Runtime specialized for JIT code execution.

	asmjit::CodeHolder code;                        // Holds code and relocation information.
	code.init(rt.getCodeInfo());					// Initialize to the same arch as JIT runtime.

	asmjit::X86Assembler a(&code);                  // Create and attach X86Assembler to `code`.
	a.mov(asmjit::x86::eax, 1);                     // Move one to 'eax' register.
	a.ret();										// Return from function.
	// ----> X86Assembler is no longer needed from here and can be destroyed <----
	
	Func fn;
	asmjit::Error err = rt.add(&fn, &code);         // Add the generated code to the runtime.
	if (err) {
		REQUIRE(false);
	}
	
	int result = fn();                      // Execute the generated code.
	REQUIRE(result == 1);

	// All classes use RAII, all resources will be released before `main()` returns,
	// the generated function can be, however, released explicitly if you intend to
	// reuse or keep the runtime alive, which you should in a production-ready code.
	rt.release(fn);
}

#include "headers/Detour/X64Detour.hpp"
#include "headers/CapstoneDisassembler.hpp"


NOINLINE void hookMe(int a) {
	volatile int var = 1;
	volatile int var2 = 0;
	var2 += 3;
	var2 = var + var2;
	var2 *= 30 / 3;
	var = 2;
	printf("%d %d\n", var, var2); // 2, 40
}
uint64_t hookMeTramp = 0;

NOINLINE void myCallback(const PLH::ILCallback::Parameters* params) {
	printf("holy balls it works: %d\n", (int)params->m_arguments[0]); 
}

TEST_CASE("Minimal ILCallback", "[AsmJit][ILCallback]") {
	PLH::ILCallback callback;

	// void func(int), ABI must match hooked function
	asmjit::FuncSignature sig;
	std::vector<uint8_t> args = { asmjit::TypeIdOf<int>::kTypeId};
	sig.init(asmjit::CallConv::kIdHost, asmjit::TypeIdOf<void>::kTypeId, args.data(), (uint32_t)args.size());
	uint64_t JIT = callback.getJitFunc(sig, &myCallback);

	PLH::CapstoneDisassembler dis(PLH::Mode::x64);
	PLH::x64Detour detour((char*)&hookMe, (char*)JIT, &hookMeTramp, dis);
	REQUIRE(detour.hook() == true);
	hookMe(1337);
}