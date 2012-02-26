#ifndef HPP_PSI_CLASS
#define HPP_PSI_CLASS

#include "Compiler.hpp"

namespace Psi {
  namespace Compiler {
    /**
     * Class member information required by both ClassInfo and ClassMemberInfo.
     */
    struct ClassMemberInfoCommon {
      /// Static data value.
      TreePtr<Term> static_value;
      /// Member data type.
      TreePtr<Term> member_type;
      /// Callback to be used when this member is accessed statically.
      TreePtr<MacroDotCallback> static_callback;
      /// Callback to be used when this member is accessed on an object.
      TreePtr<MacroDotCallback> member_callback;

      template<typename V>
      static void visit(V& v) {
        v("static_value", &ClassMemberInfoCommon::static_value)
        ("member_type", &ClassMemberInfoCommon::member_type)
        ("static_callback", &ClassMemberInfoCommon::static_callback)
        ("member_callback", &ClassMemberInfoCommon::member_callback);
      }
    };
    
    /**
     * Data supplied by class members.
     */
    struct ClassMemberInfo : ClassMemberInfoCommon {
      /// Implementations
      PSI_STD::vector<TreePtr<Implementation> > object_implementations;
      /// Static implementations
      PSI_STD::vector<TreePtr<Implementation> > static_implementations;

      template<typename V>
      static void visit(V& v) {
        visit_base<ClassMemberInfoCommon>(v);
        v("object_implementations", &ClassMemberInfo::object_implementations)
        ("static_implementations", &ClassMemberInfo::static_implementations);
      }
    };

    struct ClassMemberNamed : ClassMemberInfoCommon {
      String name;

      template<typename V>
      static void visit(V& v) {
        visit_base<ClassMemberInfoCommon>(v);
        v("name", &ClassMemberNamed::name);
      }
    };

    struct ClassInfo {
      /// Collection of all implementations in this class (since implementations do not have names)
      PSI_STD::vector<TreePtr<Implementation> > object_implementations;
      /// Collection of all static implementations in this class.
      PSI_STD::vector<TreePtr<Implementation> > static_implementations;
      /// List of members, which may or may not be named, however it is an error if non-empty names are not unique.
      PSI_STD::vector<ClassMemberNamed> members;

      template<typename V>
      static void visit(V& v) {
        v("object_implementations", &ClassInfo::object_implementations)
        ("static_implementations", &ClassInfo::static_implementations)
        ("members", &ClassInfo::members);
      }
    };

    class ClassMemberInfoCallback;
    
    struct ClassMemberInfoCallbackVtable {
      TreeVtable base;
      void (*class_member_info) (ClassMemberInfo*, const ClassMemberInfoCallback*);
    };

    /**
     * Tree type used to get class member information.
     */
    class ClassMemberInfoCallback : public Tree {
    public:
      typedef ClassMemberInfoCallbackVtable VtableType;
      static const SIVtable vtable;
      
      ClassMemberInfoCallback(const ClassMemberInfoCallbackVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }

      ClassMemberInfo class_member_info() const {
        ResultStorage<ClassMemberInfo> result;
        derived_vptr(this)->class_member_info(result.ptr(), this);
        return result.done();
      }
    };
    
    template<typename Derived, typename Impl=Derived>
    struct ClassMemberInfoCallbackWrapper {
      static void class_member_info(ClassMemberInfo *result, const ClassMemberInfoCallback *self) {
        new (result) ClassMemberInfo(Impl::class_member_info_impl(*static_cast<const Derived*>(self)));
      }
    };
    
#define PSI_COMPILER_CLASS_MEMBER_INFO_CALLBACK(derived,name,super) { \
    PSI_COMPILER_TREE(derived,name,super), \
    &ClassMemberInfoCallbackWrapper<derived>::class_member_info \
  }

    class ClassMutator;

    struct ClassMutatorVtable {
      TreeVtable base;
      void (*before) (const ClassMutator*, ClassInfo*);
      void (*after) (const ClassMutator*, ClassInfo*);
    };

    /**
     * Tree type which supports class mutator callbacks.
     */
    class ClassMutator : public Tree {
    public:
      typedef ClassMutatorVtable VtableType;
      static const SIVtable vtable;

      ClassMutator(const ClassMutatorVtable *vptr, CompileContext& compile_context, const SourceLocation& location)
      : Tree(PSI_COMPILER_VPTR_UP(Tree, vptr), compile_context, location) {
      }
      
      /**
       * Called before class member processing.
       */
      void before(ClassInfo& class_info) const {
        derived_vptr(this)->before(this, &class_info);
      }

      /**
       * Called after class member processing.
       */
      void after(ClassInfo& class_info) const {
        derived_vptr(this)->after(this, &class_info);
      }
    };
  }
}

#endif
