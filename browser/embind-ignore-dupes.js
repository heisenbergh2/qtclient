/**
 * Soft wasm --post-js: EMSCRIPTEN_BINDINGS(builtin) re-registers primitives
 * after client/tibia/wasm_embind_bootstrap.cpp (constructor(101)). Treat
 * duplicate typeid registrations as no-ops so ctors can finish.
 */
(function () {
  if (typeof sharedRegisterType !== 'function') return;
  const orig = sharedRegisterType;
  sharedRegisterType = function (rawType, registeredInstance, options = {}) {
    options = Object.assign({ ignoreDuplicateRegistrations: true }, options);
    return orig(rawType, registeredInstance, options);
  };
})();
