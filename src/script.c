#include "script.h"
#include "script_struct.h"
#include "backends.h"
#include "stats.h"

#include "rmutil/alloc.h"
#include "util/arr_rm_alloc.h"

RedisModuleType *RedisAI_ScriptType = NULL;

static void* RAI_Script_RdbLoad(struct RedisModuleIO *io, int encver) {
  // if (encver != RAI_ENC_VER) {
  //   /* We should actually log an error here, or try to implement
  //      the ability to load older versions of our data structure. */
  //   return NULL;
  // }

  RAI_Error err = {0};

  const char *devicestr = RedisModule_LoadStringBuffer(io, NULL);
  const char *tag = RedisModule_LoadStringBuffer(io, NULL);

  size_t len;
  char *scriptdef = RedisModule_LoadStringBuffer(io, &len);

  RAI_Script *script = RAI_ScriptCreate(devicestr, tag, scriptdef, &err);

  if (err.code == RAI_EBACKENDNOTLOADED) {
    RedisModuleCtx* ctx = RedisModule_GetContextFromIO(io);
    int ret = RAI_LoadDefaultBackend(ctx, RAI_BACKEND_TORCH);
    if (ret == REDISMODULE_ERR) {
      RedisModule_Log(ctx, "error", "Could not load default TORCH backend\n");
      RAI_ClearError(&err);
      return NULL;
    }
    RAI_ClearError(&err);
    script = RAI_ScriptCreate(devicestr, tag, scriptdef, &err);
  }
 
  RedisModule_Free(scriptdef);

  if (err.code != RAI_OK) {
    printf("ERR: %s\n", err.detail);
    RAI_ClearError(&err);
  }

  RedisModuleCtx* stats_ctx = RedisModule_GetContextFromIO(io);
  RedisModuleString* stats_keystr = RedisModule_CreateStringFromString(stats_ctx,
                                                                       RedisModule_GetKeyNameFromIO(io));
  const char* stats_devicestr = RedisModule_Strdup(devicestr);
  const char* stats_tag = RedisModule_Strdup(tag);

  script->infokey = RAI_AddStatsEntry(stats_ctx, stats_keystr, RAI_SCRIPT, RAI_BACKEND_TORCH, stats_devicestr, stats_tag);

  RedisModule_Free(stats_keystr);

  return script;
}

static void RAI_Script_RdbSave(RedisModuleIO *io, void *value) {
  RAI_Script *script = (RAI_Script*)value;

  size_t len = strlen(script->scriptdef) + 1;

  RedisModule_SaveStringBuffer(io, script->devicestr, strlen(script->devicestr) + 1);
  RedisModule_SaveStringBuffer(io, script->tag, strlen(script->tag) + 1);
  RedisModule_SaveStringBuffer(io, script->scriptdef, len);
}

static void RAI_Script_AofRewrite(RedisModuleIO *aof, RedisModuleString *key, void *value) {
  RAI_Script *script = (RAI_Script*)value;

  RedisModule_EmitAOF(aof, "AI.SCRIPTSET", "sccc", key, script->devicestr, script->tag, script->scriptdef);
}

static void RAI_Script_DTFree(void *value) {
  RAI_Error err = {0};
  RAI_ScriptFree(value, &err);
  if (err.code != RAI_OK) {
    printf("ERR: %s\n", err.detail);
    RAI_ClearError(&err);
  }
}

int RAI_ScriptInit(RedisModuleCtx* ctx) {
  RedisModuleTypeMethods tmScript = {
      .version = REDISMODULE_TYPE_METHOD_VERSION,
      .rdb_load = RAI_Script_RdbLoad,
      .rdb_save = RAI_Script_RdbSave,
      .aof_rewrite = RAI_Script_AofRewrite,
      .mem_usage = NULL,
      .free = RAI_Script_DTFree,
      .digest = NULL
  };

  RedisAI_ScriptType = RedisModule_CreateDataType(ctx, "AI_SCRIPT", 0, &tmScript);
  return RedisAI_ScriptType != NULL;
}

RAI_Script *RAI_ScriptCreate( const char* devicestr, const char* tag, const char *scriptdef, RAI_Error* err) {
  if (!RAI_backends.torch.script_create) {
    RAI_SetError(err, RAI_EBACKENDNOTLOADED, "ERR Backend not loaded: TORCH");
    return NULL;
  }
  RAI_Script* script = RAI_backends.torch.script_create(devicestr, scriptdef, err);

  if (script) {
    script->tag = RedisModule_Strdup(tag);
  }

  return script;
}

void RAI_ScriptFree(RAI_Script* script, RAI_Error* err) {
  if (--script->refCount > 0){
    return;
  }

  if (!RAI_backends.torch.script_free) {
    RAI_SetError(err, RAI_EBACKENDNOTLOADED, "ERR Backend not loaded: TORCH");
    return;
  }

  RedisModule_Free(script->tag);

  RAI_RemoveStatsEntry(script->infokey);
 
  RAI_backends.torch.script_free(script, err);
}

RAI_ScriptRunCtx* RAI_ScriptRunCtxCreate(RAI_Script* script, const char *fnname) {
#define PARAM_INITIAL_SIZE 10
  RAI_ScriptRunCtx* sctx = RedisModule_Calloc(1, sizeof(*sctx));
  sctx->script = RAI_ScriptGetShallowCopy(script);
  sctx->inputs = array_new(RAI_ScriptCtxParam, PARAM_INITIAL_SIZE);
  sctx->outputs = array_new(RAI_ScriptCtxParam, PARAM_INITIAL_SIZE);
  size_t fnname_len = strlen(fnname);
  sctx->fnname = RedisModule_Calloc(fnname_len, sizeof(char));
  memcpy(sctx->fnname, fnname, fnname_len);
  return sctx;
}

static int Script_RunCtxAddParam(RAI_ScriptRunCtx* sctx, RAI_ScriptCtxParam* paramArr,
                                 RAI_Tensor* tensor) {

  RAI_ScriptCtxParam param = {
      .tensor = tensor ? RAI_TensorGetShallowCopy(tensor): NULL,
  };
  paramArr = array_append(paramArr, param);
  return 1;
}

int RAI_ScriptRunCtxAddInput(RAI_ScriptRunCtx* sctx, RAI_Tensor* inputTensor) {
  return Script_RunCtxAddParam(sctx, sctx->inputs, inputTensor);
}

int RAI_ScriptRunCtxAddOutput(RAI_ScriptRunCtx* sctx) {
  return Script_RunCtxAddParam(sctx, sctx->outputs, NULL);
}

size_t RAI_ScriptRunCtxNumOutputs(RAI_ScriptRunCtx* sctx) {
  return array_len(sctx->outputs);
}

RAI_Tensor* RAI_ScriptRunCtxOutputTensor(RAI_ScriptRunCtx* sctx, size_t index) {
  assert(RAI_ScriptRunCtxNumOutputs(sctx) > index && index >= 0);
  return sctx->outputs[index].tensor;
}

void RAI_ScriptRunCtxFree(RAI_ScriptRunCtx* sctx) {
  for (size_t i = 0 ; i < array_len(sctx->inputs) ; ++i) {
    RAI_TensorFree(sctx->inputs[i].tensor);
  }
  array_free(sctx->inputs);

  for (size_t i = 0 ; i < array_len(sctx->outputs) ; ++i) {
    if (sctx->outputs[i].tensor) {
      RAI_TensorFree(sctx->outputs[i].tensor);
    }
  }
  array_free(sctx->outputs);

  RedisModule_Free(sctx->fnname);

  RAI_Error err = {0};
  RAI_ScriptFree(sctx->script, &err);

  if (err.code != RAI_OK) {
    // TODO: take it to client somehow
    printf("ERR: %s\n", err.detail);
    RAI_ClearError(&err);
  }

  RedisModule_Free(sctx);
}

int RAI_ScriptRun(RAI_ScriptRunCtx* sctx, RAI_Error* err) {
  if (!RAI_backends.torch.script_run) {
    RAI_SetError(err, RAI_EBACKENDNOTLOADED, "ERR Backend not loaded: TORCH");
    return REDISMODULE_ERR;
  }
 
  return RAI_backends.torch.script_run(sctx, err);
}

RAI_Script* RAI_ScriptGetShallowCopy(RAI_Script* script) {
  ++script->refCount;
  return script;
}


/* Return REDISMODULE_ERR if there was an error getting the Script.
 * Return REDISMODULE_OK if the model value stored at key was correctly
 * returned and available at *model variable. */
int RAI_GetScriptFromKeyspace(RedisModuleCtx *ctx, RedisModuleString *keyName,
                              RedisModuleKey **key, RAI_Script **script,
                              int mode) {
  *key = RedisModule_OpenKey(ctx, keyName, mode);
  if (RedisModule_KeyType(*key) == REDISMODULE_KEYTYPE_EMPTY) {
    RedisModule_CloseKey(*key);
    RedisModule_ReplyWithError(ctx, "ERR script key is empty");
    return REDISMODULE_ERR;
  }
  if (RedisModule_ModuleTypeGetType(*key) != RedisAI_ScriptType) {
    RedisModule_CloseKey(*key);
    RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    return REDISMODULE_ERR;
  }
  *script = RedisModule_ModuleTypeGetValue(*key);
  return REDISMODULE_OK;
}
