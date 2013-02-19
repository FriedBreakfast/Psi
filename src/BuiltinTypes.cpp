#include "Compiler.hpp"
#include "Tree.hpp"
#include "Macros.hpp"

namespace Psi {
  namespace Compiler {
    BuiltinTypes::BuiltinTypes() {
    }
    
    namespace {
      class MakeTagCallback {
        SIType m_value_type;
        TreePtr<Term> m_wildcard_type;
        TreePtr<> m_default_value;
        
      public:
        typedef MetadataType TreeResultType;
        
        MakeTagCallback(const SIType& value_type, const TreePtr<Term>& wildcard_type, const TreePtr<>& default_value)
        : m_value_type(value_type), m_wildcard_type(wildcard_type), m_default_value(default_value) {
        }
        
        TreePtr<MetadataType> evaluate(const TreePtr<MetadataType>& self) {
          TreePtr<Term> wildcard(new Parameter(m_wildcard_type, 0, 0, self.location()));
          PSI_STD::vector<TreePtr<Term> > pattern(1, wildcard);
          PSI_STD::vector<TreePtr<Metadata> > values;
          if (m_default_value) {
            TreePtr<Term> impl_wildcard_1(new Parameter(self.compile_context().builtins().metatype, 0, 0, self.location()));
            TreePtr<Term> impl_wildcard_2(new Parameter(impl_wildcard_1, 0, 1, self.location()));
            PSI_STD::vector<TreePtr<Term> > impl_pattern(1, impl_wildcard_2);
            values.push_back(TreePtr<Metadata>(new Metadata(m_default_value, self, 2, impl_pattern, self.location())));
          }
          return TreePtr<MetadataType>(new MetadataType(self.compile_context(), 0, pattern, values, m_value_type, self.location()));
        }
        
        template<typename V>
        static void visit(V& v) {
          v("value_type", &MakeTagCallback::m_value_type)
          ("wildcard_type", &MakeTagCallback::m_wildcard_type)
          ("default_value", &MakeTagCallback::m_default_value);
        }
      };
      
      template<typename TreeType>
      TreePtr<MetadataType> make_tag(const TreePtr<Term>& wildcard_type, const SourceLocation& location, const TreePtr<>& default_value=TreePtr<>()) {
        return tree_callback(wildcard_type.compile_context(), location, MakeTagCallback(reinterpret_cast<const SIVtable*>(&TreeType::vtable), wildcard_type, default_value));
      }
      
      class MovableCopyableGenericMaker {
        bool m_movable;
        
      public:
        typedef GenericType TreeResultType;
        
        MovableCopyableGenericMaker(bool movable) : m_movable(movable) {}
        
        TreePtr<GenericType> evaluate(const TreePtr<GenericType>& self) {
          const BuiltinTypes& builtins = self.compile_context().builtins();
          CompileContext& compile_context = builtins.metatype.compile_context();
          TreePtr<Term> upref(new Parameter(builtins.upref_type, 1, 0, self.location().named_child("x0")));
          TreePtr<Term> param1(new Parameter(builtins.metatype, 1, 1, self.location().named_child("x1")));
          TreePtr<Term> param0(new Parameter(builtins.metatype, 0, 1, self.location().named_child("x1")));
          PSI_STD::vector<TreePtr<Term> > members;
          
          TreePtr<Term> self_instance(new TypeInstance(self, vector_of(upref, param1), self.location()));
          TreePtr<Term> self_derived(new DerivedType(self_instance, upref, self.location()));
          TreePtr<Term> param_ptr(new PointerType(param1, self.location()));
          
          FunctionParameterType self_derived_p(parameter_mode_input, self_derived);
          FunctionParameterType param_ptr_p(parameter_mode_functional, param_ptr);
          
          TreePtr<Term> binary_type(new FunctionType(result_mode_by_value, builtins.empty_type, vector_of(self_derived_p, param_ptr_p, param_ptr_p), default_, self.location().named_child("BinaryType")));
          TreePtr<Term> binary_ptr_type(new PointerType(binary_type, self.location().named_child("BinaryTypePtr")));

          if (m_movable) {
            TreePtr<Term> unary_type(new FunctionType(result_mode_by_value, builtins.empty_type, vector_of(self_derived_p, param_ptr_p), default_, self.location().named_child("UnaryType")));
            TreePtr<Term> unary_ptr_type(new PointerType(unary_type, self.location().named_child("UnaryTypePtr")));
            
            members.resize(5);
            members[interface_movable_init] = unary_ptr_type;
            members[interface_movable_fini] = unary_ptr_type;
            members[interface_movable_clear] = unary_ptr_type;
            members[interface_movable_move] = binary_ptr_type;
            members[interface_movable_move_init] = binary_ptr_type;
          } else {
            members.resize(3);
            members[interface_copyable_movable] = self.compile_context().builtins().movable_interface->pointer_type_after(vector_of(param0), self.location().named_child("MovableBasePointer"));
            members[interface_copyable_copy] = binary_ptr_type;
            members[interface_copyable_copy_init] = binary_ptr_type;
          }
          
          TreePtr<StructType> st(new StructType(compile_context, members, self.location()));
          return TreePtr<GenericType>(new GenericType(vector_of(upref->type, param0->type), st, default_, GenericType::primitive_always, self.location()));
        }
        
        template<typename V> static void visit(V&) {}
      };
      
      TreePtr<Interface> make_movable_copyable_interface(const BuiltinTypes& builtins, bool movable, const SourceLocation& location) {
        TreePtr<GenericType> generic = tree_callback(builtins.metatype.compile_context(), location, MovableCopyableGenericMaker(movable));
        TreePtr<Term> upref(new Parameter(builtins.upref_type, 0, 0, location.named_child("y0")));
        TreePtr<Term> param(new Parameter(builtins.metatype, 1, 0, location.named_child("y1")));
        TreePtr<TypeInstance> inst(new TypeInstance(generic, vector_of(upref, param), location));
        TreePtr<DerivedType> derived(new DerivedType(inst, upref, location));
        TreePtr<Term> exists(new Exists(derived, vector_of<TreePtr<Term> >(builtins.upref_type), location));
        
        PSI_STD::vector<Interface::InterfaceBase> bases;
        if (!movable)
          bases.push_back(Interface::InterfaceBase(builtins.movable_interface, vector_of(param), vector_of<int>(0, interface_copyable_movable)));
        
        return TreePtr<Interface>(new Interface(bases, exists, 0, vector_of<TreePtr<Term> >(builtins.metatype),
                                                default_, default_, location));
      }
    }

    void BuiltinTypes::initialize(CompileContext& compile_context) {
      SourceLocation psi_location = compile_context.root_location().named_child("psi");
      SourceLocation psi_compiler_location = psi_location.named_child("compiler");

      metatype.reset(new Metatype(compile_context, psi_location.named_child("Type")));
      empty_type.reset(new EmptyType(compile_context, psi_location.named_child("EmptyType")));
      empty_value.reset(new DefaultValue(empty_type, psi_location.named_child("Empty")));
      bottom_type.reset(new BottomType(compile_context, psi_location.named_child("Bottom")));
      upref_type.reset(new UpwardReferenceType(compile_context, psi_location.named_child("UpwardReference")));
      
      SourceLocation macro_location = psi_compiler_location.named_child("Macro");
      macro_tag = make_tag<Macro>(metatype, macro_location, default_macro_impl(compile_context, macro_location));
      library_tag = make_tag<Library>(metatype, psi_compiler_location.named_child("Library"));
      namespace_tag = make_tag<Namespace>(metatype, psi_compiler_location.named_child("Namespace"));
      
      /// \todo Need to add default implementations of Movable and Copyable for builtin types
      movable_interface = make_movable_copyable_interface(*this, true, psi_compiler_location.named_child("Movable"));
      copyable_interface = make_movable_copyable_interface(*this, false, psi_compiler_location.named_child("Copyable"));
    }
  }
}
