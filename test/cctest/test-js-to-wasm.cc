// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>

#include "include/v8.h"
#include "src/api/api.h"
#include "src/wasm/wasm-module-builder.h"
#include "test/cctest/cctest.h"
#include "test/cctest/test-api.h"
#include "test/common/wasm/flag-utils.h"
#include "test/common/wasm/test-signatures.h"
#include "test/common/wasm/wasm-macro-gen.h"

namespace v8 {
namespace internal {
namespace wasm {

static const int kDeoptLoopCount = 1e4;

// Validates the type of the result returned by a test function.
template <typename T>
bool CheckType(v8::Local<v8::Value> result) {
  return result->IsNumber();
}
template <>
bool CheckType<void>(v8::Local<v8::Value> result) {
  return result->IsUndefined();
}
template <>
bool CheckType<int>(v8::Local<v8::Value> result) {
  return result->IsInt32();
}
template <>
bool CheckType<int64_t>(v8::Local<v8::Value> result) {
  return result->IsBigInt();
}
template <>
bool CheckType<v8::Local<v8::BigInt>>(v8::Local<v8::Value> result) {
  return result->IsBigInt();
}

static TestSignatures sigs;

struct ExportedFunction {
  std::string name;
  FunctionSig* signature;
  std::vector<ValueType> locals;
  std::vector<uint8_t> code;
};

#define WASM_CODE(...) __VA_ARGS__

#define DECLARE_EXPORTED_FUNCTION(name, sig, code) \
  static ExportedFunction k_##name = {#name, sig, {}, code};

#define DECLARE_EXPORTED_FUNCTION_WITH_LOCALS(name, sig, locals, code) \
  static ExportedFunction k_##name = {#name, sig, locals, code};

DECLARE_EXPORTED_FUNCTION(nop, sigs.v_v(), WASM_CODE({WASM_NOP}))

DECLARE_EXPORTED_FUNCTION(i32_square, sigs.i_i(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                     kExprI32Mul}))

DECLARE_EXPORTED_FUNCTION(i64_square, sigs.l_l(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                     kExprI64Mul}))

DECLARE_EXPORTED_FUNCTION(f32_square, sigs.f_f(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                     kExprF32Mul}))

DECLARE_EXPORTED_FUNCTION(f64_square, sigs.d_d(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                     kExprF64Mul}))

DECLARE_EXPORTED_FUNCTION(void_square, sigs.v_i(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(0),
                                     kExprI32Mul, kExprDrop}))

DECLARE_EXPORTED_FUNCTION(add, sigs.i_ii(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                     kExprI32Add}))

DECLARE_EXPORTED_FUNCTION(i64_add, sigs.l_ll(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                     kExprI64Add}))

DECLARE_EXPORTED_FUNCTION(sum3, sigs.i_iii(),
                          WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(1),
                                     WASM_LOCAL_GET(2), kExprI32Add,
                                     kExprI32Add}))

DECLARE_EXPORTED_FUNCTION(no_args, sigs.i_v(), WASM_CODE({WASM_I32V(42)}))

DECLARE_EXPORTED_FUNCTION(load_i32, sigs.i_i(),
                          WASM_CODE({WASM_LOAD_MEM(MachineType::Int32(),
                                                   WASM_LOCAL_GET(0))}))
DECLARE_EXPORTED_FUNCTION(load_i64, sigs.l_l(),
                          WASM_CODE({WASM_I64_SCONVERT_I32(WASM_LOAD_MEM(
                              MachineType::Int32(),
                              WASM_I32_CONVERT_I64(WASM_LOCAL_GET(0))))}))
DECLARE_EXPORTED_FUNCTION(load_f32, sigs.f_f(),
                          WASM_CODE({WASM_F32_SCONVERT_I32(WASM_LOAD_MEM(
                              MachineType::Int32(),
                              WASM_I32_SCONVERT_F32(WASM_LOCAL_GET(0))))}))
DECLARE_EXPORTED_FUNCTION(load_f64, sigs.d_d(),
                          WASM_CODE({WASM_F64_SCONVERT_I32(WASM_LOAD_MEM(
                              MachineType::Int32(),
                              WASM_I32_SCONVERT_F64(WASM_LOCAL_GET(0))))}))
DECLARE_EXPORTED_FUNCTION(store_i32, sigs.v_ii(),
                          WASM_CODE({WASM_STORE_MEM(MachineType::Int32(),
                                                    WASM_LOCAL_GET(0),
                                                    WASM_LOCAL_GET(1))}))

// int32_t test(int32_t v0, int32_t v1, int32_t v2, int32_t v3, int32_t v4,
//              int32_t v5, int32_t v6, int32_t v7, int32_t v8, int32_t v9) {
//   return v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9;
// }
static const ValueType kIntTypes11[11] = {
    kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32,
    kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32};
static FunctionSig i_iiiiiiiiii(1, 10, kIntTypes11);
DECLARE_EXPORTED_FUNCTION(
    sum10, &i_iiiiiiiiii,
    WASM_CODE({WASM_LOCAL_GET(0), WASM_LOCAL_GET(1), WASM_LOCAL_GET(2),
               WASM_LOCAL_GET(3), WASM_LOCAL_GET(4), WASM_LOCAL_GET(5),
               WASM_LOCAL_GET(6), WASM_LOCAL_GET(7), WASM_LOCAL_GET(8),
               WASM_LOCAL_GET(9), kExprI32Add, kExprI32Add, kExprI32Add,
               kExprI32Add, kExprI32Add, kExprI32Add, kExprI32Add, kExprI32Add,
               kExprI32Add}))

// double test(int32_t i32, int64_t i64, float f32, double f64) {
//   return i32 + i64 + f32 + f64;
// }
static const ValueType kMixedTypes5[5] = {kWasmF64, kWasmI32, kWasmI64,
                                          kWasmF32, kWasmF64};
static FunctionSig d_ilfd(1, 4, kMixedTypes5);
DECLARE_EXPORTED_FUNCTION(
    sum_mixed, &d_ilfd,
    WASM_CODE({WASM_LOCAL_GET(2), kExprF64ConvertF32, WASM_LOCAL_GET(3),
               kExprF64Add, WASM_LOCAL_GET(0), kExprF64UConvertI32, kExprF64Add,
               WASM_LOCAL_GET(1), kExprF64UConvertI64, kExprF64Add}))

// float f32_square_deopt(float f32) {
//   static int count = 0;
//   if (++count == kDeoptLoopCount) {
//      callback(f32);
//   }
//   return f32 * f32;
// }
DECLARE_EXPORTED_FUNCTION_WITH_LOCALS(
    f32_square_deopt, sigs.f_f(), {kWasmI32},
    WASM_CODE(
        {WASM_STORE_MEM(
             MachineType::Int32(), WASM_I32V(1024),
             WASM_LOCAL_TEE(1, WASM_I32_ADD(WASM_LOAD_MEM(MachineType::Int32(),
                                                          WASM_I32V(1024)),
                                            WASM_ONE))),
         WASM_BLOCK(
             WASM_BR_IF(0, WASM_I32_NE(WASM_LOCAL_GET(1),
                                       WASM_I32V(kDeoptLoopCount))),
             WASM_CALL_FUNCTION(0, WASM_F64_CONVERT_F32(WASM_LOCAL_GET(0)))),
         WASM_F32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))}))

// double f64_square_deopt(double f64) {
//   static int count = 0;
//   if (++count == kDeoptLoopCount) {
//      callback(f64);
//   }
//   return f64 * f64;
// }
DECLARE_EXPORTED_FUNCTION_WITH_LOCALS(
    f64_square_deopt, sigs.d_d(), {kWasmI32},
    WASM_CODE(
        {WASM_STORE_MEM(
             MachineType::Int32(), WASM_I32V(1028),
             WASM_LOCAL_TEE(1, WASM_I32_ADD(WASM_LOAD_MEM(MachineType::Int32(),
                                                          WASM_I32V(1028)),
                                            WASM_ONE))),
         WASM_BLOCK(WASM_BR_IF(0, WASM_I32_NE(WASM_LOCAL_GET(1),
                                              WASM_I32V(kDeoptLoopCount))),
                    WASM_CALL_FUNCTION(0, WASM_LOCAL_GET(0))),
         WASM_F64_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))}))

// int32_t i32_square_deopt(int32_t i32) {
//   static int count = 0;
//   if (++count == kDeoptLoopCount) {
//      callback(i32);
//   }
//   return i32 * i32;
// }
DECLARE_EXPORTED_FUNCTION_WITH_LOCALS(
    i32_square_deopt, sigs.i_i(), {kWasmI32},
    WASM_CODE(
        {WASM_STORE_MEM(
             MachineType::Int32(), WASM_I32V(1032),
             WASM_LOCAL_TEE(1, WASM_I32_ADD(WASM_LOAD_MEM(MachineType::Int32(),
                                                          WASM_I32V(1032)),
                                            WASM_ONE))),
         WASM_BLOCK(
             WASM_BR_IF(0, WASM_I32_NE(WASM_LOCAL_GET(1),
                                       WASM_I32V(kDeoptLoopCount))),
             WASM_CALL_FUNCTION(0, WASM_F64_SCONVERT_I32(WASM_LOCAL_GET(0)))),
         WASM_I32_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))}))

// int64_t i64_square_deopt(int64_t i64) {
//   static int count = 0;
//   if (++count == kDeoptLoopCount) {
//      callback(i64);
//   }
//   return i64 * i64;
// }
DECLARE_EXPORTED_FUNCTION_WITH_LOCALS(
    i64_square_deopt, sigs.l_l(), {kWasmI32},
    WASM_CODE(
        {WASM_STORE_MEM(
             MachineType::Int32(), WASM_I32V(1036),
             WASM_LOCAL_TEE(1, WASM_I32_ADD(WASM_LOAD_MEM(MachineType::Int32(),
                                                          WASM_I32V(1036)),
                                            WASM_ONE))),
         WASM_BLOCK(
             WASM_BR_IF(0, WASM_I32_NE(WASM_LOCAL_GET(1),
                                       WASM_I32V(kDeoptLoopCount))),
             WASM_CALL_FUNCTION(0, WASM_F64_SCONVERT_I64(WASM_LOCAL_GET(0)))),
         WASM_I64_MUL(WASM_LOCAL_GET(0), WASM_LOCAL_GET(0))}))

// void void_square_deopt(int32_t i32) {
//   static int count = 0;
//   if (++count == kDeoptLoopCount) {
//     callback(i32);
//   }
// }
DECLARE_EXPORTED_FUNCTION_WITH_LOCALS(
    void_square_deopt, sigs.v_i(), {kWasmI32},
    WASM_CODE(
        {WASM_STORE_MEM(
             MachineType::Int32(), WASM_I32V(1040),
             WASM_LOCAL_TEE(1, WASM_I32_ADD(WASM_LOAD_MEM(MachineType::Int32(),
                                                          WASM_I32V(1040)),
                                            WASM_ONE))),
         WASM_BLOCK(
             WASM_BR_IF(0, WASM_I32_NE(WASM_LOCAL_GET(1),
                                       WASM_I32V(kDeoptLoopCount))),
             WASM_CALL_FUNCTION(0, WASM_F64_SCONVERT_I32(WASM_LOCAL_GET(0))))}))

class FastJSWasmCallTester {
 public:
  FastJSWasmCallTester()
      : allow_natives_syntax_(&i::FLAG_allow_natives_syntax, true),
        inline_js_wasm_calls_(&i::FLAG_turbo_inline_js_wasm_calls, true),
        stress_background_compile_(&i::FLAG_stress_background_compile, false),
        allocator_(),
        zone_(&allocator_, ZONE_NAME),
        builder_(zone_.New<WasmModuleBuilder>(&zone_)) {}

  void DeclareCallback(const char* name, FunctionSig* signature,
                       const char* module) {
    builder_->AddImport(CStrVector(name), signature, CStrVector(module));
  }

  void AddExportedFunction(const ExportedFunction& exported_func) {
    WasmFunctionBuilder* func = builder_->AddFunction(exported_func.signature);
    for (auto& wasm_type : exported_func.locals) func->AddLocal(wasm_type);
    func->EmitCode(exported_func.code.data(),
                   static_cast<uint32_t>(exported_func.code.size()));
    func->Emit(kExprEnd);
    builder_->AddExport(CStrVector(exported_func.name.c_str()),
                        kExternalFunction, func->func_index());
  }

  // Executes a test function that returns a value of type T.
  template <typename T>
  void CallAndCheckWasmFunction(const std::string& exported_function_name,
                                const std::vector<v8::Local<v8::Value>>& args,
                                const T& expected_result,
                                bool test_lazy_deopt = false) {
    LocalContext env;

    v8::Local<v8::Value> result_value = DoCallAndCheckWasmFunction(
        env, exported_function_name, args, test_lazy_deopt);

    CHECK(CheckType<T>(result_value));
    T result = ConvertJSValue<T>::Get(result_value, env.local()).ToChecked();
    CHECK_EQ(result, expected_result);
  }

  // Executes a test function that returns NaN.
  void CallAndCheckWasmFunctionNaN(
      const std::string& exported_function_name,
      const std::vector<v8::Local<v8::Value>>& args,
      bool test_lazy_deopt = false) {
    LocalContext env;
    v8::Local<v8::Value> result_value = DoCallAndCheckWasmFunction(
        env, exported_function_name, args, test_lazy_deopt);

    CHECK(CheckType<double>(result_value));
    double result =
        ConvertJSValue<double>::Get(result_value, env.local()).ToChecked();
    CHECK(std::isnan(result));
  }

  // Executes a test function that returns a BigInt.
  void CallAndCheckWasmFunctionBigInt(
      const std::string& exported_function_name,
      const std::vector<v8::Local<v8::Value>>& args,
      const v8::Local<v8::BigInt> expected_result,
      bool test_lazy_deopt = false) {
    LocalContext env;
    v8::Local<v8::Value> result_value = DoCallAndCheckWasmFunction(
        env, exported_function_name, args, test_lazy_deopt);

    CHECK(CheckType<v8::Local<v8::BigInt>>(result_value));
    auto result =
        ConvertJSValue<v8::BigInt>::Get(result_value, env.local()).ToChecked();
    CHECK_EQ(result->Int64Value(), expected_result->Int64Value());
  }

  // Executes a test function that returns void.
  void CallAndCheckWasmFunction(const std::string& exported_function_name,
                                const std::vector<v8::Local<v8::Value>>& args,
                                bool test_lazy_deopt = false) {
    LocalContext env;
    v8::Local<v8::Value> result_value = DoCallAndCheckWasmFunction(
        env, exported_function_name, args, test_lazy_deopt);

    CHECK(test_lazy_deopt ? result_value->IsNumber() /* NaN */
                          : result_value->IsUndefined());
  }

  // Executes a test function that triggers eager deoptimization.
  template <typename T>
  T CallAndCheckWasmFunctionWithEagerDeopt(
      const std::string& exported_function_name, const std::string& arg,
      const T& expected_result, const std::string& deopt_arg) {
    LocalContext env;
    v8::Isolate* isolate = CcTest::isolate();
    v8::TryCatch try_catch(isolate);

    std::string js_code =
        "const importObj = {"
        "  env: {"
        "    callback : function(num) {}"
        "  }"
        "};"
        "let buf = new Uint8Array(" +
        WasmModuleAsJSArray() +
        ");"
        "let module = new WebAssembly.Module(buf);"
        "let instance = new WebAssembly.Instance(module, importObj);"
        "function test(value) {"
        "  return instance.exports." +
        exported_function_name +
        "(value);"
        "}"
        "%PrepareFunctionForOptimization(test);"
        "test(" +
        arg +
        ");"
        "%OptimizeFunctionOnNextCall(test);"
        "test(" +
        arg + ");";

    v8::Local<v8::Value> result_value = CompileRun(js_code.c_str());
    CHECK(CheckType<T>(result_value));
    T result = ConvertJSValue<T>::Get(result_value, env.local()).ToChecked();
    CHECK_EQ(result, expected_result);

    std::string deopt_code = "test(" + deopt_arg + ");";
    result_value = CompileRun(deopt_code.c_str());
    CHECK(CheckType<T>(result_value));
    return ConvertJSValue<T>::Get(result_value, env.local()).ToChecked();
  }

  // Executes a test function that throws an exception.
  void CallAndCheckExceptionCaught(const std::string& exported_function_name,
                                   const v8::Local<v8::Value> arg) {
    LocalContext env;
    CHECK((*env)->Global()->Set(env.local(), v8_str("arg"), arg).FromJust());

    v8::Isolate* isolate = CcTest::isolate();
    v8::TryCatch try_catch(isolate);

    std::string js_code =
        "const importObj = {"
        "  env: {"
        "    callback : function(num) {}"
        "  }"
        "};"
        "let buf = new Uint8Array(" +
        WasmModuleAsJSArray() +
        ");"
        "let module = new WebAssembly.Module(buf);"
        "let instance = new WebAssembly.Instance(module, importObj);"
        "let " +
        exported_function_name + " = instance.exports." +
        exported_function_name +
        ";"
        "function test() {"
        "  return " +
        exported_function_name +
        "(arg);"
        "}"
        "%PrepareFunctionForOptimization(test);"
        "test();";

    CompileRun(js_code.c_str());
    CHECK(try_catch.HasCaught());

    try_catch.Reset();
    CompileRun("%OptimizeFunctionOnNextCall(test); test();");
    CHECK(try_catch.HasCaught());
  }

  // Executes a test function with a try/catch.
  void CallAndCheckWithTryCatch(const std::string& exported_function_name,
                                const v8::Local<v8::Value> arg) {
    LocalContext env;
    CHECK((*env)->Global()->Set(env.local(), v8_str("arg"), arg).FromJust());

    std::string js_code =
        "const importObj = {"
        "  env: {"
        "    callback : function(num) {}"
        "  }"
        "};"
        "let buf = new Uint8Array(" +
        WasmModuleAsJSArray() +
        ");"
        "let module = new WebAssembly.Module(buf);"
        "let instance = new WebAssembly.Instance(module, importObj);"
        "let " +
        exported_function_name + " = instance.exports." +
        exported_function_name +
        ";"
        "function test() {"
        "  try {"
        "    return " +
        exported_function_name +
        "(arg);"
        "  } catch (e) {"
        "    return 0;"
        "  }"
        "}"
        "%PrepareFunctionForOptimization(test);"
        "test();";
    v8::Local<v8::Value> result_value_interpreted = CompileRun(js_code.c_str());
    CHECK(CheckType<int32_t>(result_value_interpreted));
    auto result_interpreted =
        ConvertJSValue<int32_t>::Get(result_value_interpreted, env.local())
            .ToChecked();

    v8::Local<v8::Value> result_value_compiled = CompileRun(
        "%OptimizeFunctionOnNextCall(test);"
        "test();");
    CHECK(CheckType<int32_t>(result_value_compiled));
    auto result_compiled =
        ConvertJSValue<int32_t>::Get(result_value_compiled, env.local())
            .ToChecked();

    CHECK_EQ(result_interpreted, result_compiled);
  }

  // Executes a test function with a try/catch calling a Wasm function returning
  // void.
  void CallAndCheckWithTryCatch_void(const std::string& exported_function_name,
                                     const v8::Local<v8::Value> arg0,
                                     const v8::Local<v8::Value> arg1) {
    LocalContext env;
    CHECK((*env)->Global()->Set(env.local(), v8_str("arg0"), arg0).FromJust());
    CHECK((*env)->Global()->Set(env.local(), v8_str("arg1"), arg1).FromJust());

    std::string js_code =
        "const importObj = {"
        "  env: {"
        "    callback : function(num) {}"
        "  }"
        "};"
        "let buf = new Uint8Array(" +
        WasmModuleAsJSArray() +
        ");"
        "let module = new WebAssembly.Module(buf);"
        "let instance = new WebAssembly.Instance(module, importObj);"
        "let " +
        exported_function_name + " = instance.exports." +
        exported_function_name +
        ";"
        "function test() {"
        "  try {"
        "    " +
        exported_function_name +
        "(arg0, arg1);"
        "    return 1;"
        "  } catch (e) {"
        "    return 0;"
        "  }"
        "}"
        "%PrepareFunctionForOptimization(test);"
        "test();";
    v8::Local<v8::Value> result_value_interpreted = CompileRun(js_code.c_str());
    CHECK(CheckType<int32_t>(result_value_interpreted));
    auto result_interpreted =
        ConvertJSValue<int32_t>::Get(result_value_interpreted, env.local())
            .ToChecked();

    v8::Local<v8::Value> result_value_compiled = CompileRun(
        "%OptimizeFunctionOnNextCall(test);"
        "test();");
    CHECK(CheckType<int32_t>(result_value_compiled));
    auto result_compiled =
        ConvertJSValue<int32_t>::Get(result_value_compiled, env.local())
            .ToChecked();

    CHECK_EQ(result_interpreted, result_compiled);
  }

 private:
  // Convert the code of a Wasm module into a string that represents the content
  // of a JavaScript Uint8Array, that can be loaded with
  // WebAssembly.Module(buf).
  std::string WasmModuleAsJSArray() {
    ZoneBuffer buffer(&zone_);
    builder_->WriteTo(&buffer);

    std::stringstream string_stream;
    string_stream << "[";
    auto it = buffer.begin();
    if (it != buffer.end()) {
      string_stream << "0x" << std::setfill('0') << std::setw(2) << std::hex
                    << static_cast<int>(*it++);
    }
    while (it != buffer.end()) {
      string_stream << ", 0x" << std::setfill('0') << std::setw(2) << std::hex
                    << static_cast<int>(*it++);
    }
    string_stream << "]";
    return string_stream.str();
  }

  v8::Local<v8::Value> DoCallAndCheckWasmFunction(
      LocalContext& env, const std::string& exported_function_name,
      const std::vector<v8::Local<v8::Value>>& args,
      bool test_lazy_deopt = false) {
    for (size_t i = 0; i < args.size(); i++) {
      CHECK((*env)
                ->Global()
                ->Set(env.local(), v8_str(("arg" + std::to_string(i)).c_str()),
                      args[i])
                .FromJust());
    }

    std::string js_code =
        test_lazy_deopt
            ? GetJSTestCodeWithLazyDeopt(env, WasmModuleAsJSArray(),
                                         exported_function_name, args.size())
            : GetJSTestCode(WasmModuleAsJSArray(), exported_function_name,
                            args.size());
    return CompileRun(js_code.c_str());
  }

  // Format the JS test code that loads and instantiates a Wasm module and
  // calls a Wasm exported function, making sure that it is compiled by
  // TurboFan:
  //
  // function test() {"
  //   let result = exported_func(arg0, arg1, ..., argN-1);
  //   return result;"
  // }
  std::string GetJSTestCode(const std::string& wasm_module,
                            const std::string& wasm_exported_function_name,
                            size_t arity) {
    std::string js_args = ArgsToString(arity);
    return "const importObj = {"
           "  env: { callback : function(num) {} }"
           "};"
           "let buf = new Uint8Array(" +
           wasm_module +
           ");"
           "let module = new WebAssembly.Module(buf);"
           "let instance = new WebAssembly.Instance(module, importObj);"
           "let " +
           wasm_exported_function_name + " = instance.exports." +
           wasm_exported_function_name +
           ";"
           "function test() {"
           "  let result = " +
           wasm_exported_function_name + "(" + js_args +
           ");"
           "  return result;"
           "}"
           "%PrepareFunctionForOptimization(test);"
           "test(" +
           js_args +
           ");"
           "%OptimizeFunctionOnNextCall(test);"
           "test(" +
           js_args + ");";
  }

  // Format the JS test code that loads and instantiates a Wasm module and
  // calls a Wasm exported function in a loop, and it's compiled with TurboFan:
  //
  // var b = 0;"
  // var n = 0;"
  // function test() {"
  //   let result = 0;
  //   for(var i = 0; i < 1e5; i++) {
  //     result = exported_func(arg0 + b) + n;
  //   }
  //   return result;"
  // }
  //
  // Here the Wasm function calls back into a JavaScript function that modifies
  // the values of 'b' and 'n', triggering the lazy deoptimization of the 'test'
  // function.
  std::string GetJSTestCodeWithLazyDeopt(
      LocalContext& env, const std::string& wasm_module,
      const std::string& wasm_exported_function_name, size_t arity) {
    DCHECK_LE(arity, 1);
    bool bigint_arg = false;
    if (arity == 1) {
      v8::Local<v8::Value> arg0 =
          (*env)->Global()->Get(env.local(), v8_str("arg0")).ToLocalChecked();
      bigint_arg = arg0->IsBigInt();
    }

    std::string js_args = ArgsToString(arity);
    std::string code =
        "const importObj = {"
        "  env: {"
        "    callback : function(num) {"
        "      n = 1;  b = 1;"
        "    }"
        "  }"
        "};"
        "let buf = new Uint8Array(" +
        wasm_module +
        ");"
        "let module = new WebAssembly.Module(buf);"
        "let instance = new WebAssembly.Instance(module, importObj);"
        "let " +
        wasm_exported_function_name + " = instance.exports." +
        wasm_exported_function_name +
        ";"
        "var b = 0;"
        "var n = 0;"
        "function test(" +
        js_args +
        ") {"
        "  var result = 0;"
        "  for (let i = 0; i < " +
        std::to_string(kDeoptLoopCount) + " + 5; i++) {";
    code += bigint_arg ? "    result = " + wasm_exported_function_name + "(" +
                             js_args + "+ BigInt(b)) + BigInt(n);"
                       : "    result = " + wasm_exported_function_name + "(" +
                             js_args + "+ b) + n;";
    code +=
        "  }"
        "  return result;"
        "}"
        "test(" +
        js_args + ");";

    return code;
  }

  // Format a string that represents the set of arguments passed to a test
  // function, in the form 'arg0, arg1, ..., argN-1'.
  // The value of these args is set by GetJSTestCodeWithLazyDeopt.
  std::string ArgsToString(size_t arity) {
    std::stringstream string_stream;
    for (size_t i = 0; i < arity; i++) {
      if (i > 0) string_stream << ", ";
      string_stream << "arg" << i;
    }
    return string_stream.str();
  }

  i::FlagScope<bool> allow_natives_syntax_;
  i::FlagScope<bool> inline_js_wasm_calls_;
  i::FlagScope<bool> stress_background_compile_;
  AccountingAllocator allocator_;
  Zone zone_;
  WasmModuleBuilder* builder_;
};

TEST(TestFastJSWasmCall_Nop) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_nop);
  tester.CallAndCheckWasmFunction("nop", {});
}

TEST(TestFastJSWasmCall_I32Arg) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_i32_square);
  tester.CallAndCheckWasmFunction<int32_t>("i32_square", {v8_num(42)}, 42 * 42);
}

TEST(TestFastJSWasmCall_I32ArgNotSmi) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_add);
  tester.CallAndCheckWasmFunction<int32_t>(
      "add", {v8_num(0x7fffffff), v8_int(1)}, 0x80000000);
}

TEST(TestFastJSWasmCall_F32Arg) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_f32_square);
  tester.CallAndCheckWasmFunction<float>("f32_square", {v8_num(42.0)},
                                         42.0 * 42.0);
}

TEST(TestFastJSWasmCall_F64Arg) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_f64_square);
  tester.CallAndCheckWasmFunction<double>("f64_square", {v8_num(42.0)},
                                          42.0 * 42.0);
}

TEST(TestFastJSWasmCall_I64Arg) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_i64_square);
  tester.CallAndCheckWasmFunctionBigInt("i64_square", {v8_bigint(1234567890ll)},
                                        v8_bigint(1234567890ll * 1234567890ll));
}

TEST(TestFastJSWasmCall_I64NegativeResult) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_i64_add);
  tester.CallAndCheckWasmFunctionBigInt(
      "i64_add", {v8_bigint(1ll), v8_bigint(-2ll)}, v8_bigint(-1ll));
}

TEST(TestFastJSWasmCall_MultipleArgs) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_sum10);
  tester.CallAndCheckWasmFunction<int32_t>(
      "sum10",
      {v8_num(1), v8_num(2), v8_num(3), v8_num(4), v8_num(5), v8_num(6),
       v8_num(7), v8_num(8), v8_num(9), v8_num(10)},
      55);
}

TEST(TestFastJSWasmCall_MixedArgs) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_sum_mixed);
  tester.CallAndCheckWasmFunction<double>(
      "sum_mixed", {v8_num(1), v8_bigint(0x80000000), v8_num(42.0), v8_num(.5)},
      1 + 0x80000000 + 42 + .5);
}

TEST(TestFastJSWasmCall_MistypedArgs) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;

  tester.AddExportedFunction(k_i32_square);
  tester.CallAndCheckWasmFunction<int32_t>("i32_square", {v8_str("test")}, 0);
}

TEST(TestFastJSWasmCall_MixedMistypedArgs) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;

  tester.AddExportedFunction(k_sum_mixed);
  tester.CallAndCheckWasmFunctionNaN(
      "sum_mixed", {v8_str("alpha"), v8_bigint(0x80000000), v8_str("beta"),
                    v8_str("gamma")});
}

TEST(TestFastJSWasmCall_NoArgs) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;

  tester.AddExportedFunction(k_no_args);
  tester.CallAndCheckWasmFunction<int32_t>("no_args", {}, 42);
}

TEST(TestFastJSWasmCall_NoReturnTypes) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;

  tester.AddExportedFunction(k_void_square);
  tester.CallAndCheckWasmFunction("void_square", {v8_num(42)});
}

TEST(TestFastJSWasmCall_MismatchedArity) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;

  tester.AddExportedFunction(k_sum3);
  tester.CallAndCheckWasmFunction<int32_t>("sum3", {v8_num(1), v8_num(2)}, 3);
  tester.CallAndCheckWasmFunction<int32_t>(
      "sum3",
      {v8_num(1), v8_num(2), v8_num(3), v8_num(4), v8_num(5), v8_num(6)}, 6);
  tester.CallAndCheckWasmFunction<int32_t>("sum3", {}, 0);
}

// Lazy deoptimization tests

TEST(TestFastJSWasmCall_LazyDeopt_I32Result) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.DeclareCallback("callback", sigs.v_d(), "env");
  tester.AddExportedFunction(k_i32_square_deopt);
  tester.CallAndCheckWasmFunction<int32_t>("i32_square_deopt", {v8_num(42)},
                                           43 * 43 + 1, true);
}

TEST(TestFastJSWasmCall_LazyDeopt_I64Result) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.DeclareCallback("callback", sigs.v_d(), "env");
  tester.AddExportedFunction(k_i64_square_deopt);

  tester.CallAndCheckWasmFunctionBigInt("i64_square_deopt", {v8_bigint(42)},
                                        v8_bigint(43 * 43 + 1), true);

  // This test would fail if the result was converted into a HeapNumber through
  // a double, losing precision.
  tester.CallAndCheckWasmFunctionBigInt(
      "i64_square_deopt", {v8_bigint(1234567890ll)},
      v8_bigint(1524157877488187882ll),  // (1234567890 + 1)*(1234567890 + 1)+1
      true);
}

TEST(TestFastJSWasmCall_LazyDeopt_F32Result) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.DeclareCallback("callback", sigs.v_d(), "env");
  tester.AddExportedFunction(k_f32_square_deopt);
  tester.CallAndCheckWasmFunction<float>("f32_square_deopt", {v8_num(42.0)},
                                         43 * 43 + 1, true);
}

TEST(TestFastJSWasmCall_LazyDeopt_F64Result) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.DeclareCallback("callback", sigs.v_d(), "env");
  tester.AddExportedFunction(k_f64_square_deopt);
  tester.CallAndCheckWasmFunction<float>("f64_square_deopt", {v8_num(42.0)},
                                         43 * 43 + 1, true);
}

TEST(TestFastJSWasmCall_LazyDeopt_VoidResult) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.DeclareCallback("callback", sigs.v_d(), "env");
  tester.AddExportedFunction(k_void_square_deopt);
  tester.CallAndCheckWasmFunction("void_square_deopt", {v8_num(42.0)}, true);
}

// Eager deoptimization tests

TEST(TestFastJSWasmCall_EagerDeopt) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_f32_square);
  float result_after_deopt =
      tester.CallAndCheckWasmFunctionWithEagerDeopt<float>(
          "f32_square", "42", 42.0 * 42.0, "{x:1,y:2}");
  CHECK(std::isnan(result_after_deopt));
}

// Exception handling tests

TEST(TestFastJSWasmCall_Trap_i32) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_load_i32);
  tester.CallAndCheckWithTryCatch("load_i32", {v8_int(0x7fffffff)});
}

TEST(TestFastJSWasmCall_Trap_i64) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_load_i64);
  tester.CallAndCheckWithTryCatch("load_i64", {v8_bigint(0x7fffffff)});
}

TEST(TestFastJSWasmCall_Trap_f32) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_load_f32);
  tester.CallAndCheckWithTryCatch("load_f32", {v8_num(0x7fffffff)});
}

TEST(TestFastJSWasmCall_Trap_f64) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_load_f64);
  tester.CallAndCheckWithTryCatch("load_f64", {v8_num(0x7fffffff)});
}

TEST(TestFastJSWasmCall_Trap_void) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_store_i32);
  tester.CallAndCheckWithTryCatch_void("store_i32", v8_int(0x7fffffff),
                                       v8_int(42));
}

// BigInt

TEST(TestFastJSWasmCall_I64ArgExpectsBigInt) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_i64_square);
  tester.CallAndCheckExceptionCaught("i64_square", v8_int(42));
}

TEST(TestFastJSWasmCall_F32ArgDoesntExpectBigInt) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_f32_square);
  tester.CallAndCheckExceptionCaught("f32_square", v8_bigint(42ll));
}

TEST(TestFastJSWasmCall_F64ArgDoesntExpectBigInt) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_f64_square);
  tester.CallAndCheckExceptionCaught("f64_square", v8_bigint(42ll));
}

TEST(TestFastJSWasmCall_I32ArgDoesntExpectBigInt) {
  v8::HandleScope scope(CcTest::isolate());
  FastJSWasmCallTester tester;
  tester.AddExportedFunction(k_i32_square);
  tester.CallAndCheckExceptionCaught("i32_square", v8_bigint(42ll));
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8