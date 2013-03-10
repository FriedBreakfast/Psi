#include "Compiler.hpp"
#include "Tree.hpp"
#include "Macros.hpp"
#include "TermBuilder.hpp"
#include <json_object.h>

namespace Psi {
  namespace Compiler {
    BuiltinTypes::BuiltinTypes() {
    }
    
    namespace {
      template<typename TreeType>
      TreePtr<MetadataType> make_tag(const TreePtr<Term>& wildcard_type, const SourceLocation& location, const TreePtr<>& default_value=TreePtr<>()) {
        TreePtr<Term> wildcard = TermBuilder::parameter(wildcard_type, 0, 0, location);
        PSI_STD::vector<TreePtr<Term> > pattern(1, wildcard);
        PSI_STD::vector<TreePtr<Metadata> > values;
        if (default_value) {
          TreePtr<Term> impl_wildcard_1 = TermBuilder::parameter(wildcard_type.compile_context().builtins().metatype, 0, 0, location);
          TreePtr<Term> impl_wildcard_2 = TermBuilder::parameter(impl_wildcard_1, 0, 1, location);
          PSI_STD::vector<TreePtr<Term> > impl_pattern(1, impl_wildcard_2);
          values.push_back(Metadata::new_(default_value, default_, 2, impl_pattern, location));
        }
        return MetadataType::new_(wildcard_type.compile_context(), 0, pattern, values, reinterpret_cast<const SIVtable*>(&TreeType::vtable), location);
      }
      
      class MovableCopyableGenericMaker {
        bool m_movable;
        
      public:
        typedef GenericType TreeResultType;
        
        MovableCopyableGenericMaker(bool movable) : m_movable(movable) {}
        
        TreePtr<Term> evaluate(const TreePtr<GenericType>& self) {
          const BuiltinTypes& builtins = self.compile_context().builtins();
          CompileContext& compile_context = builtins.metatype.compile_context();
          TreePtr<Anonymous> upref = TermBuilder::anonymous(builtins.upref_type, term_mode_value, self.location().named_child("x0"));
          TreePtr<Anonymous> param = TermBuilder::anonymous(builtins.metatype, term_mode_value, self.location().named_child("x1"));
          PSI_STD::vector<TreePtr<Term> > members;
          
          TreePtr<Term> self_instance = TermBuilder::instance(self, vector_of<TreePtr<Term> >(upref, param), self.location());
          TreePtr<Term> self_derived = TermBuilder::derived(self_instance, upref, self.location());
          TreePtr<Term> param_ptr = TermBuilder::pointer(param, self.location());
          
          FunctionParameterType self_derived_p(parameter_mode_input, self_derived);
          FunctionParameterType param_ptr_p(parameter_mode_functional, param_ptr);
          
          TreePtr<Term> binary_type = TermBuilder::function_type(result_mode_by_value, builtins.empty_type, vector_of(self_derived_p, param_ptr_p, param_ptr_p), default_, self.location().named_child("BinaryType"));
          TreePtr<Term> binary_ptr_type = TermBuilder::pointer(binary_type, self.location().named_child("BinaryTypePtr"));

          if (m_movable) {
            TreePtr<Term> unary_type = TermBuilder::function_type(result_mode_by_value, builtins.empty_type, vector_of(self_derived_p, param_ptr_p), default_, self.location().named_child("UnaryType"));
            TreePtr<Term> unary_ptr_type = TermBuilder::pointer(unary_type, self.location().named_child("UnaryTypePtr"));
            
            members.resize(5);
            members[interface_movable_init] = unary_ptr_type;
            members[interface_movable_fini] = unary_ptr_type;
            members[interface_movable_clear] = unary_ptr_type;
            members[interface_movable_move] = binary_ptr_type;
            members[interface_movable_move_init] = binary_ptr_type;
          } else {
            members.resize(3);
            members[interface_copyable_movable] = self.compile_context().builtins().movable_interface->pointer_type_after(vector_of<TreePtr<Term> >(param), self.location().named_child("MovableBasePointer"));
            members[interface_copyable_copy] = binary_ptr_type;
            members[interface_copyable_copy_init] = binary_ptr_type;
          }
          
          return TermBuilder::struct_type(compile_context, members, self.location())->parameterize(self.location(), vector_of(upref, param));
        }
        
        template<typename V> static void visit(V&) {}
      };
      
      TreePtr<Interface> make_movable_copyable_interface(const BuiltinTypes& builtins, bool movable, const SourceLocation& location) {
        TreePtr<GenericType> generic = TermBuilder::generic(builtins.metatype.compile_context(), vector_of<TreePtr<Term> >(builtins.upref_type, builtins.metatype),
                                                            GenericType::primitive_always, location, MovableCopyableGenericMaker(movable));
        TreePtr<Term> upref = TermBuilder::parameter(builtins.upref_type, 0, 0, location.named_child("y0"));
        TreePtr<Term> param = TermBuilder::parameter(builtins.metatype, 1, 0, location.named_child("y1"));
        TreePtr<TypeInstance> inst = TermBuilder::instance(generic, vector_of(upref, param), location);
        TreePtr<Term> derived = TermBuilder::derived(inst, upref, location);
        TreePtr<Term> exists = TermBuilder::exists(derived, vector_of<TreePtr<Term> >(builtins.upref_type), location);
        
        PSI_STD::vector<Interface::InterfaceBase> bases;
        if (!movable)
          bases.push_back(Interface::InterfaceBase(builtins.movable_interface, vector_of(param), vector_of<int>(0, interface_copyable_movable)));
        
        return Interface::new_(bases, exists, 0, vector_of<TreePtr<Term> >(builtins.metatype), default_, default_, location);
      }
    }

    void BuiltinTypes::initialize(CompileContext& compile_context) {
      SourceLocation psi_location = compile_context.root_location().named_child("psi");
      SourceLocation psi_compiler_location = psi_location.named_child("compiler");

      metatype = compile_context.get_functional(Metatype(), psi_location.named_child("Type"));
      empty_type = compile_context.get_functional(EmptyType(), psi_location.named_child("EmptyType"));
      empty_value = TermBuilder::default_value(empty_type, psi_location.named_child("Empty"));
      bottom_type = compile_context.get_functional(BottomType(), psi_location.named_child("Bottom"));
      upref_type = compile_context.get_functional(UpwardReferenceType(), psi_location.named_child("UpwardReference"));
      size_type = compile_context.get_functional(PrimitiveType("core.uint.ptr"), psi_location.named_child("Size"));
      string_element_type = compile_context.get_functional(PrimitiveType("core.uint.8"), psi_location.named_child("Unsigned8"));
      boolean_type = compile_context.get_functional(PrimitiveType("core.bool"), psi_location.named_child("Bool"));
      
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
