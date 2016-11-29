/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2016 Facebook, Inc. (http://www.facebook.com)     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/
#ifndef incl_HPHP_MINSTR_HELPERS_H_
#define incl_HPHP_MINSTR_HELPERS_H_

#include "hphp/runtime/base/ref-data.h"
#include "hphp/runtime/base/typed-value.h"
#include "hphp/runtime/vm/member-operations.h"

#include "hphp/runtime/vm/jit/array-offset-profile.h"
#include "hphp/runtime/vm/jit/translator-runtime.h"

#include "hphp/runtime/ext/collections/ext_collections-map.h"

namespace HPHP { namespace jit { namespace MInstrHelpers {

//////////////////////////////////////////////////////////////////////

template<MOpMode mode>
TypedValue* baseGImpl(TypedValue key) {
  auto const name = prepareKey(key);
  SCOPE_EXIT { decRefStr(name); };

  auto const varEnv = g_context->m_globalVarEnv;
  assertx(varEnv != nullptr);

  auto base = varEnv->lookup(name);
  if (base == nullptr) {
    if (mode == MOpMode::Warn) {
      raise_notice(Strings::UNDEFINED_VARIABLE, name->data());
    }
    if (mode == MOpMode::Define) {
      auto tv = make_tv<KindOfNull>();
      varEnv->set(name, &tv);
      base = varEnv->lookup(name);
    } else {
      return const_cast<TypedValue*>(&immutable_null_base);
    }
  }
  return tvToCell(base);
}

#define BASE_G_HELPER_TABLE(m)                  \
  /* name    mode                  */           \
  m(baseG,   MOpMode::None)                     \
  m(baseGW,  MOpMode::Warn)                     \
  m(baseGD,  MOpMode::Define)                   \

#define X(nm, mode)                             \
inline TypedValue* nm(TypedValue key) {         \
  return baseGImpl<mode>(key);                  \
}
BASE_G_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

#define PROP_HELPER_TABLE(m)                                \
  /* name      mode                  keyType     */ \
  m(propC,     MOpMode::None,       KeyType::Any)   \
  m(propCS,    MOpMode::None,       KeyType::Str)   \
  m(propCD,    MOpMode::Define,     KeyType::Any)   \
  m(propCDS,   MOpMode::Define,     KeyType::Str)   \
  m(propCU,    MOpMode::Unset,      KeyType::Any)   \
  m(propCUS,   MOpMode::Unset,      KeyType::Str)   \
  m(propCW,    MOpMode::Warn,       KeyType::Any)   \
  m(propCWS,   MOpMode::Warn,       KeyType::Str)

#define X(nm, mode, kt)                                               \
inline TypedValue* nm(Class* ctx, TypedValue* base, key_type<kt> key, \
                      TypedValue& tvRef) {                            \
  return Prop<mode,kt>(tvRef, ctx, base, key);                        \
}
PROP_HELPER_TABLE(X)
#undef X

#define PROP_OBJ_HELPER_TABLE(m)                      \
  /* name      mode                  keyType     */   \
  m(propCDO,   MOpMode::Define,     KeyType::Any)    \
  m(propCDOS,  MOpMode::Define,     KeyType::Str)    \
  m(propCO,    MOpMode::None,       KeyType::Any)    \
  m(propCOS,   MOpMode::None,       KeyType::Str)    \
  m(propCUO,   MOpMode::Unset,      KeyType::Any)    \
  m(propCUOS,  MOpMode::Unset,      KeyType::Str)    \
  m(propCWO,   MOpMode::Warn,       KeyType::Any)    \
  m(propCWOS,  MOpMode::Warn,       KeyType::Str)    \

#define X(nm, mode, kt)                                               \
inline TypedValue* nm(Class* ctx, ObjectData* base, key_type<kt> key, \
                      TypedValue& tvRef) {                            \
  return PropObj<mode,kt>(tvRef, ctx, base, key);                     \
}
PROP_OBJ_HELPER_TABLE(X)
#undef X

// NullSafe prop.
inline TypedValue* propCQ(Class* ctx, TypedValue* base, StringData* key,
                          TypedValue& tvRef) {
  return nullSafeProp(tvRef, ctx, base, key);
}

// NullSafe prop with object base.
inline TypedValue* propCOQ(Class* ctx, ObjectData* base, StringData* key,
                           TypedValue& tvRef) {
  return base->prop(&tvRef, ctx, key);
}

//////////////////////////////////////////////////////////////////////

inline TypedValue cGetRefShuffle(const TypedValue& localTvRef,
                                 const TypedValue* result) {
  if (LIKELY(result != &localTvRef)) {
    result = tvToCell(result);
    tvRefcountedIncRef(result);
  } else {
    // If a magic getter or array access method returned by reference, we have
    // to incref the inner cell and drop our reference to the RefData.
    // Otherwise we do nothing, since we already own a reference to result.
    if (UNLIKELY(localTvRef.m_type == KindOfRef)) {
      auto inner = *localTvRef.m_data.pref->tv();
      tvRefcountedIncRef(&inner);
      decRefRef(localTvRef.m_data.pref);
      return inner;
    }
  }

  return *result;
}

#define CGET_PROP_HELPER_TABLE(m)                       \
  /* name            keyType       mode  */             \
  m(cGetPropCQuiet,  KeyType::Any, MOpMode::None)      \
  m(cGetPropSQuiet,  KeyType::Str, MOpMode::None)      \
  m(cGetPropC,       KeyType::Any, MOpMode::Warn)      \
  m(cGetPropS,       KeyType::Str, MOpMode::Warn)      \

#define X(nm, kt, mode)                                                \
inline TypedValue nm(Class* ctx, TypedValue* base, key_type<kt> key) { \
  TypedValue localTvRef;                                               \
  auto result = Prop<mode,kt>(localTvRef, ctx, base, key);             \
  return cGetRefShuffle(localTvRef, result);                           \
}
CGET_PROP_HELPER_TABLE(X)
#undef X

#define CGET_OBJ_PROP_HELPER_TABLE(m)                   \
  /* name            keyType       mode */             \
  m(cGetPropCOQuiet, KeyType::Any, MOpMode::None)      \
  m(cGetPropSOQuiet, KeyType::Str, MOpMode::None)      \
  m(cGetPropCO,      KeyType::Any, MOpMode::Warn)      \
  m(cGetPropSO,      KeyType::Str, MOpMode::Warn)      \

#define X(nm, kt, mode)                                                \
inline TypedValue nm(Class* ctx, ObjectData* base, key_type<kt> key) { \
  TypedValue localTvRef;                                               \
  auto result = PropObj<mode,kt>(localTvRef, ctx, base, key);          \
  return cGetRefShuffle(localTvRef, result);                           \
}
CGET_OBJ_PROP_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

// NullSafe prop.
inline TypedValue cGetPropSQ(Class* ctx, TypedValue* base, StringData* key) {
  TypedValue localTvRef;
  auto result = nullSafeProp(localTvRef, ctx, base, key);
  return cGetRefShuffle(localTvRef, result);
}

// NullSafe prop with object base.
inline TypedValue cGetPropSOQ(Class* ctx, ObjectData* base, StringData* key) {
  TypedValue localTvRef;
  auto result = base->prop(&localTvRef, ctx, key);
  return cGetRefShuffle(localTvRef, result);
}

//////////////////////////////////////////////////////////////////////

inline RefData* vGetRefShuffle(const TypedValue& localTvRef,
                               TypedValue* result) {
  if (LIKELY(result != &localTvRef)) {
    if (result->m_type != KindOfRef) tvBox(result);
    auto ref = result->m_data.pref;
    ref->incRefCount();
    return ref;
  }

  if (localTvRef.m_type != KindOfRef) {
    // RefData::Make takes ownership of the reference that lives in localTvRef
    // so no refcounting is necessary.
    return RefData::Make(localTvRef);
  }

  return localTvRef.m_data.pref;
}

#define VGET_PROP_HELPER_TABLE(m)       \
  /* name        keyType     */\
  m(vGetPropC,   KeyType::Any)  \
  m(vGetPropS,   KeyType::Str)  \

#define X(nm, kt)                                                     \
inline RefData* nm(Class* ctx, TypedValue* base, key_type<kt> key) {  \
  TypedValue localTvRef;                                              \
  auto result = Prop<MOpMode::Define,kt>(localTvRef, ctx, base, key);\
  return vGetRefShuffle(localTvRef, result);                          \
}
VGET_PROP_HELPER_TABLE(X)
#undef X

#define VGET_OBJ_PROP_HELPER_TABLE(m)       \
  /* name        keyType      */\
  m(vGetPropCO,  KeyType::Any)  \
  m(vGetPropSO,  KeyType::Str)

#define X(nm, kt)                                                     \
inline RefData* nm(Class* ctx, ObjectData* base, key_type<kt> key) {  \
  TypedValue localTvRef;                                              \
  auto result = PropObj<MOpMode::Define,kt>(localTvRef, ctx, base, key);\
  return vGetRefShuffle(localTvRef, result);\
}
VGET_OBJ_PROP_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<class PropImpl>
void bindPropImpl(RefData* val, PropImpl prop_impl) {
  TypedValue localTvRef;
  auto prop = prop_impl(localTvRef);
  if (UNLIKELY(prop == &localTvRef)) {
    // Skip binding a TypedValue that's about to be destroyed and just destroy
    // it now.
    tvRefcountedDecRef(localTvRef);
  } else {
    tvBindRef(val, prop);
  }
}

inline void bindPropC(Class* ctx, TypedValue* base, TypedValue key,
                      RefData* val) {
  bindPropImpl(val, [&](TypedValue& tvref) {
    return Prop<MOpMode::Define,KeyType::Any>(tvref, ctx, base, key);
  });
}

inline void bindPropCO(Class* ctx, ObjectData* base, TypedValue key,
                      RefData* val) {
  bindPropImpl(val, [&](TypedValue& tvref) {
    return PropObj<MOpMode::Define,KeyType::Any>(tvref, ctx, base, key);
  });
}

//////////////////////////////////////////////////////////////////////

#define SETPROP_HELPER_TABLE(m)          \
  /* name        keyType      */         \
  m(setPropC,    KeyType::Any)           \
  m(setPropCS,   KeyType::Str)           \

#define X(nm, kt)                                                          \
inline void nm(Class* ctx, TypedValue* base, key_type<kt> key, Cell val) { \
  HPHP::SetProp<false, kt>(ctx, base, key, &val);                          \
}
SETPROP_HELPER_TABLE(X)
#undef X

#define SETPROP_OBJ_HELPER_TABLE(m)     \
  /* name        keyType     */         \
  m(setPropCO,   KeyType::Any)          \
  m(setPropCOS,  KeyType::Str)          \

#define X(nm, kt)                                                          \
inline void nm(Class* ctx, ObjectData* base, key_type<kt> key, Cell val) { \
  HPHP::SetPropObj<kt>(ctx, base, key, &val);                              \
}
SETPROP_OBJ_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

inline void unsetPropC(Class* ctx, TypedValue* base, TypedValue key) {
  HPHP::UnsetProp(ctx, base, key);
}

inline void unsetPropCO(Class* ctx, ObjectData* base, TypedValue key) {
  HPHP::UnsetPropObj(ctx, base, key);
}

//////////////////////////////////////////////////////////////////////

inline TypedValue setOpPropC(Class* ctx, TypedValue* base, TypedValue key,
                      Cell val, SetOpOp op) {
  TypedValue localTvRef;
  auto result = HPHP::SetOpProp(localTvRef, ctx, op, base, key, &val);
  return cGetRefShuffle(localTvRef, result);
}

inline TypedValue setOpPropCO(Class* ctx, ObjectData* base, TypedValue key,
                       Cell val, SetOpOp op) {
  TypedValue localTvRef;
  auto result = SetOpPropObj(localTvRef, ctx, op, base, key, &val);
  return cGetRefShuffle(localTvRef, result);
}

//////////////////////////////////////////////////////////////////////

inline TypedValue incDecPropC(Class* ctx, TypedValue* base, TypedValue key,
                              IncDecOp op) {
  auto const result = HPHP::IncDecProp(ctx, op, base, key);
  assertx(result.m_type != KindOfRef);
  return result;
}

inline TypedValue incDecPropCO(Class* ctx, ObjectData* base, TypedValue key,
                               IncDecOp op) {
  auto const result = HPHP::IncDecPropObj(ctx, op, base, key);
  assertx(result.m_type != KindOfRef);
  return result;
}

//////////////////////////////////////////////////////////////////////

#define ISSET_EMPTY_PROP_HELPER_TABLE(m)                \
  /* name          keyType       useEmpty */            \
  m(issetPropC,    KeyType::Any, false)                 \
  m(issetPropCS,   KeyType::Str, false)                 \
  m(issetPropCE,   KeyType::Any, true)                  \
  m(issetPropCES,  KeyType::Str, true)                  \

#define X(nm, kt, useEmpty)                                             \
/* This returns uint64_t to ensure all 64 bits of rax are valid. */     \
inline uint64_t nm(Class* ctx, TypedValue* base, key_type<kt> key) {    \
  return HPHP::IssetEmptyProp<useEmpty, kt>(ctx, base, key);            \
}
ISSET_EMPTY_PROP_HELPER_TABLE(X)
#undef X

#define ISSET_EMPTY_OBJ_PROP_HELPER_TABLE(m)       \
  /* name          keyType       useEmpty */       \
  m(issetPropCEO,  KeyType::Any, true)             \
  m(issetPropCEOS, KeyType::Str, true)             \
  m(issetPropCO,   KeyType::Any, false)            \
  m(issetPropCOS,  KeyType::Str, false)            \

#define X(nm, kt, useEmpty)                                             \
/* This returns uint64_t to ensure all 64 bits of rax are valid. */     \
inline uint64_t nm(Class* ctx, ObjectData* base, key_type<kt> key) {    \
  return IssetEmptyPropObj<useEmpty, kt>(ctx, base, key);               \
}
ISSET_EMPTY_OBJ_PROP_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<bool checkForInt>
void profileMixedArrayOffsetHelper(const ArrayData* ad, int64_t i,
                                   ArrayOffsetProfile* prof) {
  prof->update(ad, i);
}
template<bool checkForInt>
void profileMixedArrayOffsetHelper(const ArrayData* ad, const StringData* sd,
                                   ArrayOffsetProfile* prof) {
  prof->update(ad, sd, checkForInt);
}

#define PROFILE_MIXED_ARRAY_OFFSET_HELPER_TABLE(m)          \
  /* name                       keyType     checkForInt */  \
  m(profileMixedArrayOffsetS,  KeyType::Str,   false)       \
  m(profileMixedArrayOffsetSi, KeyType::Str,    true)       \
  m(profileMixedArrayOffsetI,  KeyType::Int,   false)       \

#define X(nm, keyType, checkForInt)                     \
inline void nm(const ArrayData* a, key_type<keyType> k, \
               ArrayOffsetProfile* p) {                 \
  profileMixedArrayOffsetHelper<checkForInt>(a, k, p);  \
}
PROFILE_MIXED_ARRAY_OFFSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

inline void profileDictOffsetHelper(const ArrayData* ad, int64_t i,
                                    ArrayOffsetProfile* prof) {
  prof->update(ad, i);
}
inline void profileDictOffsetHelper(const ArrayData* ad, const StringData* sd,
                                    ArrayOffsetProfile* prof) {
  prof->update(ad, sd, false);
}

#define PROFILE_DICT_OFFSET_HELPER_TABLE(m)                 \
  /* name                keyType  */                        \
  m(profileDictOffsetS,  KeyType::Str)                      \
  m(profileDictOffsetI,  KeyType::Int)                      \

#define X(nm, keyType)                                      \
inline void nm(const ArrayData* a, key_type<keyType> k,     \
               ArrayOffsetProfile* p) {                     \
  profileDictOffsetHelper(a, k, p);                         \
}
PROFILE_DICT_OFFSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

inline void profileKeysetOffsetHelper(const ArrayData* ad, int64_t i,
                                      ArrayOffsetProfile* prof) {
  prof->update(ad, i);
}
inline void profileKeysetOffsetHelper(const ArrayData* ad, const StringData* sd,
                                      ArrayOffsetProfile* prof) {
  prof->update(ad, sd, false);
}

#define PROFILE_KEYSET_OFFSET_HELPER_TABLE(m)               \
  /* name                keyType  */                        \
  m(profileKeysetOffsetS,  KeyType::Str)                    \
  m(profileKeysetOffsetI,  KeyType::Int)                    \

#define X(nm, keyType)                                      \
inline void nm(const ArrayData* a, key_type<keyType> k,     \
               ArrayOffsetProfile* p) {                     \
  profileKeysetOffsetHelper(a, k, p);                       \
}
PROFILE_KEYSET_OFFSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template <KeyType keyType, MOpMode mode>
TypedValue* elemImpl(TypedValue* base,
                     key_type<keyType> key,
                     TypedValue& tvRef) {
  if (mode == MOpMode::Unset) {
    return ElemU<keyType>(tvRef, base, key);
  }
  if (mode == MOpMode::Define) {
    return ElemD<mode, false, keyType>(tvRef, base, key);
  }
  // We won't really modify the TypedValue in the non-D case, so
  // this const_cast is safe.
  return const_cast<TypedValue*>(Elem<mode, keyType>(tvRef, base, key));
}

#define ELEM_HELPER_TABLE(m)                          \
  /* name      keyType         mode   */              \
  m(elemC,     KeyType::Any,   MOpMode::None)        \
  m(elemCD,    KeyType::Any,   MOpMode::Define)      \
  m(elemCU,    KeyType::Any,   MOpMode::Unset)       \
  m(elemCW,    KeyType::Any,   MOpMode::Warn)        \
  m(elemI,     KeyType::Int,   MOpMode::None)        \
  m(elemID,    KeyType::Int,   MOpMode::Define)      \
  m(elemIU,    KeyType::Int,   MOpMode::Unset)       \
  m(elemIW,    KeyType::Int,   MOpMode::Warn)        \
  m(elemS,     KeyType::Str,   MOpMode::None)        \
  m(elemSD,    KeyType::Str,   MOpMode::Define)      \
  m(elemSU,    KeyType::Str,   MOpMode::Unset)       \
  m(elemSW,    KeyType::Str,   MOpMode::Warn)        \

#define X(nm, keyType, mode)                               \
inline TypedValue* nm(TypedValue* base,                    \
                      key_type<keyType> key,               \
                      TypedValue& tvRef) {                 \
  return elemImpl<keyType,mode>(base, key, tvRef);         \
}
ELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<bool warn>
inline const TypedValue* checkedGet(ArrayData* a, StringData* key) {
  int64_t i;
  return UNLIKELY(a->convertKey(key, i)) ?
    (warn ? a->nvTryGet(i) : a->nvGet(i)) :
    (warn ? a->nvTryGet(key) : a->nvGet(key));
}

template<bool warn>
inline const TypedValue* checkedGet(ArrayData* a, int64_t key) {
  not_reached();
}

//////////////////////////////////////////////////////////////////////

template<MOpMode mode>
NEVER_INLINE
const TypedValue* elemArrayNotFound(int64_t k) {
  if (mode == MOpMode::Warn) {
    raise_notice("Undefined index: %" PRId64, k);
  }
  return &immutable_uninit_base;
}

template<MOpMode mode>
NEVER_INLINE
const TypedValue* elemArrayNotFound(const StringData* k) {
  if (mode == MOpMode::Warn) {
    raise_notice("Undefined index: %s", k->data());
  }
  return &immutable_uninit_base;
}

template<KeyType keyType, bool checkForInt, MOpMode mode>
inline const TypedValue* elemArrayImpl(ArrayData* ad, key_type<keyType> key) {
  auto constexpr warn = mode == MOpMode::Warn;
  auto const ret = checkForInt ?
    checkedGet<warn>(ad, key) :
    (warn ? ad->nvTryGet(key) : ad->nvGet(key));
  if (!ret) return elemArrayNotFound<mode>(key);
  return ret;
}

#define ELEM_ARRAY_D_HELPER_TABLE(m) \
  /* name                keyType */  \
  m(elemArraySD,    KeyType::Str)    \
  m(elemArrayID,    KeyType::Int)    \

#define X(nm, keyType)                                                 \
inline TypedValue* nm(TypedValue* base, key_type<keyType> key) {       \
  auto cbase = tvToCell(base);                                         \
  assertx(isArrayType(cbase->m_type));                                 \
  return ElemDArray<MOpMode::None, false, keyType>(cbase, key);       \
}
ELEM_ARRAY_D_HELPER_TABLE(X)
#undef X

#define ELEM_ARRAY_U_HELPER_TABLE(m) \
  /* name        keyType */          \
  m(elemArraySU, KeyType::Str)       \
  m(elemArrayIU, KeyType::Int)       \

#define X(nm, keyType)                                                 \
inline TypedValue* nm(TypedValue* base, key_type<keyType> key) {       \
  auto cbase = tvToCell(base);                                         \
  assertx(isArrayType(cbase->m_type));                                 \
  return ElemUArray<keyType>(cbase, key);                              \
}
ELEM_ARRAY_U_HELPER_TABLE(X)
#undef X

#define ELEM_ARRAY_HELPER_TABLE(m)                              \
  /* name               keyType  checkForInt   mode */          \
  m(elemArrayS,    KeyType::Str,       false,  MOpMode::None)  \
  m(elemArraySi,   KeyType::Str,        true,  MOpMode::None)  \
  m(elemArrayI,    KeyType::Int,       false,  MOpMode::None)  \
  m(elemArraySW,   KeyType::Str,       false,  MOpMode::Warn)  \
  m(elemArraySiW,  KeyType::Str,        true,  MOpMode::Warn)  \
  m(elemArrayIW,   KeyType::Int,       false,  MOpMode::Warn)  \

#define X(nm, keyType, checkForInt, mode)                           \
inline const TypedValue* nm(ArrayData* ad, key_type<keyType> key) { \
  return elemArrayImpl<keyType, checkForInt, mode>(ad, key);       \
}
ELEM_ARRAY_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

// Keep these error handlers in sync with ArrayData::getNotFound();
TypedValue arrayGetNotFound(int64_t k);
TypedValue arrayGetNotFound(const StringData* k);

template<KeyType keyType, bool checkForInt>
TypedValue arrayGetImpl(ArrayData* a, key_type<keyType> key) {
  auto ret = checkForInt ? checkedGet<true>(a, key) : a->nvTryGet(key);
  if (ret) return *ret;
  return arrayGetNotFound(key);
}

#define ARRAYGET_HELPER_TABLE(m)                           \
  /* name        keyType     checkForInt */                \
  m(arrayGetS,  KeyType::Str,   false)                     \
  m(arrayGetSi, KeyType::Str,    true)                     \
  m(arrayGetI,  KeyType::Int,   false)                     \

#define X(nm, keyType, checkForInt)                          \
inline TypedValue nm(ArrayData* a, key_type<keyType> key) {  \
  return arrayGetImpl<keyType, checkForInt>(a, key);         \
}
ARRAYGET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

#define ELEM_DICT_D_HELPER_TABLE(m)  \
  /* name           keyType */       \
  m(elemDictSD,    KeyType::Str)     \
  m(elemDictID,    KeyType::Int)     \

#define X(nm, keyType)                                                 \
inline TypedValue* nm(TypedValue* base, key_type<keyType> key) {       \
  auto cbase = tvToCell(base);                                         \
  assertx(isDictType(cbase->m_type));                                  \
  return ElemDDict<false, keyType>(cbase, key);                        \
}
ELEM_DICT_D_HELPER_TABLE(X)
#undef X

#define ELEM_DICT_U_HELPER_TABLE(m)  \
  /* name        keyType */          \
  m(elemDictSU, KeyType::Str)        \
  m(elemDictIU, KeyType::Int)        \

#define X(nm, keyType)                                                 \
inline TypedValue* nm(TypedValue* base, key_type<keyType> key) {       \
  auto cbase = tvToCell(base);                                         \
  assertx(isDictType(cbase->m_type));                                  \
  return ElemUDict<keyType>(cbase, key);                               \
}
ELEM_DICT_U_HELPER_TABLE(X)
#undef X

#define ELEM_DICT_HELPER_TABLE(m)                               \
  /* name               keyType        mode */                  \
  m(elemDictS,     KeyType::Str,       MOpMode::None)          \
  m(elemDictI,     KeyType::Int,       MOpMode::None)          \
  m(elemDictSW,    KeyType::Str,       MOpMode::Warn)          \
  m(elemDictIW,    KeyType::Int,       MOpMode::Warn)          \

#define X(nm, keyType, mode)                                       \
inline const TypedValue* nm(ArrayData* ad, key_type<keyType> key) { \
  return HPHP::ElemDict<mode, keyType>(ad, key);                   \
}
ELEM_DICT_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

#define ELEM_KEYSET_U_HELPER_TABLE(m)  \
  /* name         keyType */          \
  m(elemKeysetSU, KeyType::Str)        \
  m(elemKeysetIU, KeyType::Int)        \

#define X(nm, keyType)                                                 \
inline TypedValue* nm(TypedValue* base, key_type<keyType> key) {       \
  auto cbase = tvToCell(base);                                         \
  assertx(isKeysetType(cbase->m_type));                                \
  return ElemUKeyset<keyType>(cbase, key);                             \
}
ELEM_KEYSET_U_HELPER_TABLE(X)
#undef X

#define ELEM_KEYSET_HELPER_TABLE(m)                               \
  /* name            keyType             mode */                  \
  m(elemKeysetS,     KeyType::Str,       MOpMode::None)          \
  m(elemKeysetI,     KeyType::Int,       MOpMode::None)          \
  m(elemKeysetSW,    KeyType::Str,       MOpMode::Warn)          \
  m(elemKeysetIW,    KeyType::Int,       MOpMode::Warn)          \

#define X(nm, keyType, mode)                                       \
inline const TypedValue* nm(ArrayData* ad, key_type<keyType> key) { \
  return HPHP::ElemKeyset<mode, keyType>(ad, key);                 \
}
ELEM_KEYSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

#define DICTGET_HELPER_TABLE(m)                                  \
  /* name          keyType        mode  */                       \
  m(dictGetS,      KeyType::Str,  MOpMode::Warn)                \
  m(dictGetI,      KeyType::Int,  MOpMode::Warn)                \
  m(dictGetSQuiet, KeyType::Str,  MOpMode::None)                \
  m(dictGetIQuiet, KeyType::Int,  MOpMode::None)                \

#define X(nm, keyType, mode)                                \
inline TypedValue nm(ArrayData* a, key_type<keyType> key) {  \
  return *HPHP::ElemDict<mode, keyType>(a, key);            \
}
DICTGET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

#define KEYSETGET_HELPER_TABLE(m)                                  \
  /* name            keyType        mode */                       \
  m(keysetGetS,      KeyType::Str,  MOpMode::Warn)                \
  m(keysetGetI,      KeyType::Int,  MOpMode::Warn)                \
  m(keysetGetSQuiet, KeyType::Str,  MOpMode::None)                \
  m(keysetGetIQuiet, KeyType::Int,  MOpMode::None)                \

#define X(nm, keyType, mode)                                \
inline TypedValue nm(ArrayData* a, key_type<keyType> key) {  \
  return *HPHP::ElemKeyset<mode, keyType>(a, key);          \
}
KEYSETGET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template <KeyType keyType, MOpMode mode>
TypedValue cGetElemImpl(TypedValue* base, key_type<keyType> key) {
  TypedValue localTvRef;
  auto result = Elem<mode, keyType>(localTvRef, base, key);
  return cGetRefShuffle(localTvRef, result);
}

#define CGETELEM_HELPER_TABLE(m)                  \
  /* name            key          mode  */       \
  m(cGetElemCQuiet, KeyType::Any, MOpMode::None) \
  m(cGetElemIQuiet, KeyType::Int, MOpMode::None) \
  m(cGetElemSQuiet, KeyType::Str, MOpMode::None) \
  m(cGetElemC,      KeyType::Any, MOpMode::Warn) \
  m(cGetElemI,      KeyType::Int, MOpMode::Warn) \
  m(cGetElemS,      KeyType::Str, MOpMode::Warn) \

#define X(nm, kt, mode)                                                \
inline TypedValue nm(TypedValue* base, key_type<kt> key) {              \
  return cGetElemImpl<kt, mode>(base, key);                            \
}
CGETELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template <KeyType keyType>
RefData* vGetElemImpl(TypedValue* base, key_type<keyType> key) {
  TypedValue localTvRef;
  auto result = ElemD<MOpMode::Define, true, keyType>(localTvRef, base, key);
  return vGetRefShuffle(localTvRef, result);
}

#define VGETELEM_HELPER_TABLE(m)                \
  /* name         keyType */                    \
  m(vGetElemC,    KeyType::Any)                 \
  m(vGetElemI,    KeyType::Int)                 \
  m(vGetElemS,    KeyType::Str)

#define X(nm, kt)                                                     \
inline RefData* nm(TypedValue* base, key_type<kt> key) {              \
  return vGetElemImpl<kt>(base, key);                                 \
}
VGETELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

inline ArrayData* checkedSet(ArrayData* a,
                             StringData* key,
                             Cell value,
                             bool copy) {
  int64_t i;
  return UNLIKELY(a->convertKey(key, i)) ? a->set(i, value, copy) :
         a->set(key, value, copy);
}

inline ArrayData* checkedSet(ArrayData*, int64_t, Cell, bool) {
  not_reached();
}

//////////////////////////////////////////////////////////////////////

template<KeyType keyType, bool checkForInt, bool setRef>
typename ShuffleReturn<setRef>::return_type
arraySetImpl(ArrayData* a, key_type<keyType> key, Cell value, RefData* ref) {
  static_assert(keyType != KeyType::Any,
                "KeyType::Any is not supported in arraySetMImpl");
  assertx(cellIsPlausible(value));
  assertx(a->isPHPArray());
  const bool copy = a->cowCheck();
  ArrayData* ret = checkForInt ? checkedSet(a, key, value, copy)
                               : a->set(key, value, copy);
  return arrayRefShuffle<setRef, KindOfArray>(a, ret,
                                              setRef ? ref->tv() : nullptr);
}

#define ARRAYSET_HELPER_TABLE(m)                             \
  /* name        keyType        checkForInt  setRef */       \
  m(arraySetS,   KeyType::Str,   false,      false)          \
  m(arraySetSi,  KeyType::Str,    true,      false)          \
  m(arraySetI,   KeyType::Int,   false,      false)          \
  m(arraySetSR,  KeyType::Str,   false,      true)           \
  m(arraySetSiR, KeyType::Str,    true,      true)           \
  m(arraySetIR,  KeyType::Int,   false,      true)           \

#define X(nm, keyType, checkForInt, setRef)                  \
ShuffleReturn<setRef>::return_type                           \
inline nm(ArrayData* a, key_type<keyType> key, Cell value, RefData* ref) { \
  return arraySetImpl<keyType, checkForInt, setRef>(a, key, value, ref);\
}
ARRAYSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<bool setRef>
typename ShuffleReturn<setRef>::return_type
vecSetImpl(ArrayData* a, int64_t key, Cell value, RefData* ref) {
  assertx(cellIsPlausible(value));
  assertx(a->isVecArray());
  const bool copy = a->cowCheck();
  ArrayData* ret = PackedArray::SetIntVec(a, key, value, copy);
  return arrayRefShuffle<setRef, KindOfVec>(a, ret,
                                            setRef ? ref->tv() : nullptr);
}

#define VECSET_HELPER_TABLE(m) \
  /* name      setRef */       \
  m(vecSetI,   false)          \
  m(vecSetIR,  true)           \

#define X(nm, setRef)                                                   \
ShuffleReturn<setRef>::return_type                                      \
inline nm(ArrayData* a, int64_t key, Cell value, RefData* ref) {        \
  return vecSetImpl<setRef>(a, key, value, ref);                        \
}
VECSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

inline ArrayData* dictSetImplPre(ArrayData* a, int64_t i, Cell val) {
  return MixedArray::SetIntDict(a, i, val, a->cowCheck());
}
inline ArrayData* dictSetImplPre(ArrayData* a, StringData* s, Cell val) {
  return MixedArray::SetStrDict(a, s, val, a->cowCheck());
}

template<KeyType keyType, bool setRef>
typename ShuffleReturn<setRef>::return_type
dictSetImpl(ArrayData* a, key_type<keyType> key, Cell value, RefData* ref) {
  assertx(cellIsPlausible(value));
  assertx(a->isDict());
  auto ret = dictSetImplPre(a, key, value);
  return arrayRefShuffle<setRef, KindOfDict>(a, ret,
                                             setRef ? ref->tv() : nullptr);
}

#define DICTSET_HELPER_TABLE(m) \
  /* name       keyType        setRef */         \
  m(dictSetI,   KeyType::Int,  false)            \
  m(dictSetIR,  KeyType::Int,  true)             \
  m(dictSetS,   KeyType::Str,  false)            \
  m(dictSetSR,  KeyType::Str,  true)             \

#define X(nm, keyType, setRef)                                           \
ShuffleReturn<setRef>::return_type                                       \
inline nm(ArrayData* a, key_type<keyType> key, Cell val, RefData* ref) { \
  return dictSetImpl<keyType, setRef>(a, key, val, ref);                 \
}
DICTSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

inline ArrayData* keysetSetNewElemImplPre(ArrayData* a, int64_t i) {
  return SetArray::AddToSet(a, i, a->cowCheck());
}

inline ArrayData* keysetSetNewElemImplPre(ArrayData* a, StringData* s) {
  return SetArray::AddToSet(a, s, a->cowCheck());
}

template<KeyType keyType>
void keysetSetNewElemImpl(TypedValue* tv, key_type<keyType> key) {
  assertx(tvIsPlausible(*tv));
  assertx(tvIsKeyset(tv));
  auto oldArr = tv->m_data.parr;
  auto newArr = keysetSetNewElemImplPre(oldArr, key);
  if (oldArr != newArr) {
    tv->m_type = KindOfKeyset;
    tv->m_data.parr = newArr;
    assertx(tvIsPlausible(*tv));
    decRefArr(oldArr);
  }
}

#define KEYSET_SETNEWELEM_HELPER_TABLE(m)       \
  /* name              keyType      */          \
  m(keysetSetNewElemI, KeyType::Int)            \
  m(keysetSetNewElemS, KeyType::Str)            \

#define X(nm, keyType)                                   \
inline void nm(TypedValue* tv, key_type<keyType> key) {  \
  keysetSetNewElemImpl<keyType>(tv, key);                \
}
KEYSET_SETNEWELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////
template <KeyType keyType>
StringData* setElemImpl(TypedValue* base, key_type<keyType> key, Cell val) {
  return HPHP::SetElem<false, keyType>(base, key, &val);
}

#define SETELEM_HELPER_TABLE(m)                 \
  /* name       keyType    */                   \
  m(setElemC,   KeyType::Any)                   \
  m(setElemI,   KeyType::Int)                   \
  m(setElemS,   KeyType::Str)

#define X(nm, kt)                                                     \
inline StringData* nm(TypedValue* base, key_type<kt> key, Cell val) { \
  return setElemImpl<kt>(base, key, val);                             \
}
SETELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////


template<KeyType keyType, bool checkForInt>
uint64_t arrayIssetImpl(ArrayData* a, key_type<keyType> key) {
  auto const value = checkForInt ? checkedGet<false>(a, key) : a->nvGet(key);
  return !value ? 0 :
         !tvAsCVarRef(value).isNull();
}

#define ARRAY_ISSET_HELPER_TABLE(m)                  \
  /* name           keyType         checkForInt  */  \
  m(arrayIssetS,    KeyType::Str,   false)           \
  m(arrayIssetSi,   KeyType::Str,    true)           \
  m(arrayIssetI,    KeyType::Int,   false)           \

#define X(nm, keyType, checkForInt)                    \
inline uint64_t nm(ArrayData* a, key_type<keyType> key) {\
  return arrayIssetImpl<keyType, checkForInt>(a, key);\
}
ARRAY_ISSET_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<KeyType keyType, bool isEmpty>
uint64_t dictIssetImpl(ArrayData* a, key_type<keyType> key) {
  return IssetEmptyElemDict<isEmpty, keyType>(a, key);
}

#define DICT_ISSET_EMPTY_ELEM_HELPER_TABLE(m)         \
  /* name              keyType      isEmpty */        \
  m(dictIssetElemS,    KeyType::Str,  false)          \
  m(dictIssetElemSE,   KeyType::Str,  true)           \
  m(dictIssetElemI,    KeyType::Int,  false)          \
  m(dictIssetElemIE,   KeyType::Int,  true)           \

#define X(nm, keyType, isEmpty)                               \
inline uint64_t nm(ArrayData* a, key_type<keyType> key) {     \
  return dictIssetImpl<keyType, isEmpty>(a, key);             \
}
DICT_ISSET_EMPTY_ELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<KeyType keyType, bool isEmpty>
uint64_t keysetIssetImpl(ArrayData* a, key_type<keyType> key) {
  return IssetEmptyElemKeyset<isEmpty, keyType>(a, key);
}

#define KEYSET_ISSET_EMPTY_ELEM_HELPER_TABLE(m)         \
  /* name                keyType      isEmpty */        \
  m(keysetIssetElemS,    KeyType::Str,  false)          \
  m(keysetIssetElemSE,   KeyType::Str,  true)           \
  m(keysetIssetElemI,    KeyType::Int,  false)          \
  m(keysetIssetElemIE,   KeyType::Int,  true)           \

#define X(nm, keyType, isEmpty)                               \
inline uint64_t nm(ArrayData* a, key_type<keyType> key) {     \
  return keysetIssetImpl<keyType, isEmpty>(a, key);           \
}
KEYSET_ISSET_EMPTY_ELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<KeyType keyType>
void unsetElemImpl(TypedValue* base, key_type<keyType> key) {
  HPHP::UnsetElem<keyType>(base, key);
}

#define UNSET_ELEM_HELPER_TABLE(m)              \
  /* name         keyType */                    \
  m(unsetElemC,   KeyType::Any)                 \
  m(unsetElemI,   KeyType::Int)                 \
  m(unsetElemS,   KeyType::Str)

#define X(nm, kt)                                     \
inline void nm(TypedValue* base, key_type<kt> key) {  \
  unsetElemImpl<kt>(base, key);                       \
}
UNSET_ELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////
template <KeyType keyType, bool isEmpty>
bool issetEmptyElemImpl(TypedValue* base, key_type<keyType> key) {
  return HPHP::IssetEmptyElem<isEmpty, keyType>(base, key);
}

#define ISSET_EMPTY_ELEM_HELPER_TABLE(m)        \
  /* name         keyType     isEmpty */        \
  m(issetElemC,   KeyType::Any, false)          \
  m(issetElemCE,  KeyType::Any,  true)          \
  m(issetElemI,   KeyType::Int, false)          \
  m(issetElemIE,  KeyType::Int,  true)          \
  m(issetElemS,   KeyType::Str, false)          \
  m(issetElemSE,  KeyType::Str,  true)          \

#define X(nm, kt, isEmpty)                                \
inline uint64_t nm(TypedValue* base, key_type<kt> key) {  \
  return issetEmptyElemImpl<kt, isEmpty>(base, key);      \
}
ISSET_EMPTY_ELEM_HELPER_TABLE(X)
#undef X

//////////////////////////////////////////////////////////////////////

template<KeyType keyType>
TypedValue mapGetImpl(c_Map* map, key_type<keyType> key) {
  TypedValue* ret = map->at(key);
  tvRefcountedIncRef(ret);
  return *ret;
}

template<KeyType keyType>
uint64_t mapIssetImpl(c_Map* map, key_type<keyType> key) {
  auto result = map->get(key);
  return result ? !cellIsNull(result) : false;
}

template<KeyType keyType>
void mapSetImpl(c_Map* map, key_type<keyType> key, Cell value) {
  // XXX: we should call this directly from the TC.
  map->set(key, &value);
}

//////////////////////////////////////////////////////////////////////

}}}

#endif
