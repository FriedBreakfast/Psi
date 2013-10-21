#include "Compiler.hpp"
#include "Tree.hpp"
#include "Macros.hpp"
#include "TermBuilder.hpp"

namespace Psi {
  namespace Compiler {
    BuiltinTypes::BuiltinTypes() {
    }
    
    namespace {
      TreePtr<Term> make_generic_type(const TreePtr<Term>& type, const SourceLocation& location) {
        TreePtr<GenericType> generic = TermBuilder::generic(type->compile_context(), default_, GenericType::primitive_always, location, type);
        return TermBuilder::instance(generic, default_, location);
      }
      
      TreePtr<MetadataType>
      make_tag_n(CompileContext& compile_context, const SIVtable *vptr, const PSI_STD::vector<TreePtr<Term> >& wildcard_types,
                 const SourceLocation& location, const TreePtr<>& default_value=TreePtr<>()) {
        PSI_STD::vector<TreePtr<Term> > pattern(wildcard_types.size());
        for (unsigned ii = 0, ie = wildcard_types.size(); ii != ie; ++ii)
          pattern[ii] = TermBuilder::parameter(wildcard_types[ii], 0, ii, location);
        
        PSI_STD::vector<TreePtr<Metadata> > values;
        if (default_value)
          values.push_back(Metadata::new_(default_value, default_, pattern.size(), pattern, location));
        
        return MetadataType::new_(compile_context, 0, pattern, values, vptr, location);
      }
      
      template<typename TreeType>
      TreePtr<MetadataType> make_tag(const TreePtr<Term>& wildcard_type, const SourceLocation& location, const TreePtr<>& default_value=TreePtr<>()) {
        PSI_STD::vector<TreePtr<Term> > wildcard_types(1, wildcard_type);
        return make_tag_n(wildcard_type->compile_context(), reinterpret_cast<const SIVtable*>(&TreeType::vtable), wildcard_types, location, default_value);
      }

      template<typename TreeType>
      TreePtr<MetadataType> make_tag_2(const TreePtr<Term>& wildcard_type_1, const TreePtr<Term>& wildcard_type_2, const SourceLocation& location, const TreePtr<>& default_value=TreePtr<>()) {
        PSI_STD::vector<TreePtr<Term> > wildcard_types(2);
        wildcard_types[0] = wildcard_type_1;
        wildcard_types[1] = wildcard_type_2;
        return make_tag_n(wildcard_type_1->compile_context(), reinterpret_cast<const SIVtable*>(&TreeType::vtable), wildcard_types, location, default_value);
      }
      
      class MovableCopyableGenericMaker {
        bool m_movable;
        
      public:
        typedef GenericType TreeResultType;
        
        MovableCopyableGenericMaker(bool movable) : m_movable(movable) {}
        
        TreePtr<Term> evaluate(const TreePtr<GenericType>& self) {
          const BuiltinTypes& builtins = self->compile_context().builtins();
          CompileContext& compile_context = builtins.metatype->compile_context();
          TreePtr<Anonymous> upref = TermBuilder::anonymous(builtins.upref_type, term_mode_value, self->location().named_child("x0"));
          TreePtr<Anonymous> param = TermBuilder::anonymous(builtins.metatype, term_mode_value, self->location().named_child("x1"));
          PSI_STD::vector<TreePtr<Term> > members;
          
          TreePtr<Term> self_instance = TermBuilder::instance(self, vector_of<TreePtr<Term> >(upref, param), self->location());
          TreePtr<Term> self_pointer = TermBuilder::pointer(self_instance, upref, self->location());
          
          FunctionParameterType self_derived_p(parameter_mode_functional, self_pointer);
          FunctionParameterType out_param_p(parameter_mode_output, param);
          FunctionParameterType in_param_p(parameter_mode_input, param);
          
          TreePtr<Term> binary_type = TermBuilder::function_type(result_mode_functional, builtins.empty_type, vector_of(self_derived_p, out_param_p, in_param_p), default_, self->location().named_child("BinaryType"));
          TreePtr<Term> binary_ptr_type = TermBuilder::pointer(binary_type, self->location().named_child("BinaryTypePtr"));

          if (m_movable) {
            TreePtr<Term> unary_type = TermBuilder::function_type(result_mode_functional, builtins.empty_type, vector_of(self_derived_p, out_param_p), default_, self->location().named_child("UnaryType"));
            TreePtr<Term> unary_ptr_type = TermBuilder::pointer(unary_type, self->location().named_child("UnaryTypePtr"));
            
            members.resize(5);
            members[interface_movable_init] = unary_ptr_type;
            members[interface_movable_fini] = unary_ptr_type;
            members[interface_movable_clear] = unary_ptr_type;
            members[interface_movable_move] = binary_ptr_type;
            members[interface_movable_move_init] = binary_ptr_type;
          } else {
            members.resize(3);
            members[interface_copyable_movable] = self->compile_context().builtins().movable_interface->type_after(vector_of<TreePtr<Term> >(param), self->location().named_child("MovableBasePointer"));
            members[interface_copyable_copy] = binary_ptr_type;
            members[interface_copyable_copy_init] = binary_ptr_type;
          }
          
          return TermBuilder::struct_type(compile_context, members, self->location())->parameterize(self->location(), vector_of(upref, param));
        }
        
        template<typename V> static void visit(V&) {}
      };
      
      TreePtr<Interface> make_movable_copyable_interface(const BuiltinTypes& builtins, bool movable, const SourceLocation& location) {
        TreePtr<GenericType> generic = TermBuilder::generic(builtins.metatype->compile_context(), vector_of<TreePtr<Term> >(builtins.upref_type, builtins.metatype),
                                                            GenericType::primitive_always, location, MovableCopyableGenericMaker(movable));
        TreePtr<Term> upref = TermBuilder::parameter(builtins.upref_type, 0, 0, location.named_child("y0"));
        TreePtr<Term> param = TermBuilder::parameter(builtins.metatype, 1, 0, location.named_child("y1"));
        TreePtr<TypeInstance> inst = TermBuilder::instance(generic, vector_of(upref, param), location);
        TreePtr<Term> pointer = TermBuilder::pointer(inst, upref, location);
        TreePtr<Term> exists = TermBuilder::exists(pointer, vector_of<TreePtr<Term> >(builtins.upref_type), location);
        
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
      upref_null = compile_context.get_functional(UpwardReferenceNull(), psi_location.named_child("UpwardReferenceNull"));
      
      boolean_type = compile_context.get_functional(NumberType(NumberType::n_bool), psi_location.named_child("Bool"));

      i8_type = compile_context.get_functional(NumberType(NumberType::n_i8), psi_location.named_child("Int8"));
      i16_type = compile_context.get_functional(NumberType(NumberType::n_i16), psi_location.named_child("Int16"));
      i32_type = compile_context.get_functional(NumberType(NumberType::n_i32), psi_location.named_child("Int32"));
      i64_type = compile_context.get_functional(NumberType(NumberType::n_i64), psi_location.named_child("Int64"));
      iptr_type = compile_context.get_functional(NumberType(NumberType::n_iptr), psi_location.named_child("IntPtr"));

      u8_type = compile_context.get_functional(NumberType(NumberType::n_u8), psi_location.named_child("UInt8"));
      u16_type = compile_context.get_functional(NumberType(NumberType::n_u16), psi_location.named_child("UInt16"));
      u32_type = compile_context.get_functional(NumberType(NumberType::n_u32), psi_location.named_child("UInt32"));
      u64_type = compile_context.get_functional(NumberType(NumberType::n_u64), psi_location.named_child("UInt64"));
      uptr_type = compile_context.get_functional(NumberType(NumberType::n_uptr), psi_location.named_child("UIntPtr"));

      string_element_type = u8_type;
      
      SourceLocation macro_location = psi_compiler_location.named_child("Macro");
      macro_tag = make_tag_2<Macro>(metatype, metatype, macro_location, default_macro_impl(compile_context, macro_location));
      SourceLocation type_macro_location = psi_compiler_location.named_child("TypeMacro");
      type_macro_tag = make_tag_2<Macro>(metatype, metatype, macro_location, default_type_macro_impl(compile_context, type_macro_location));
      library_tag = make_tag<Library>(metatype, psi_compiler_location.named_child("Library"));
      namespace_tag = make_tag<Namespace>(metatype, psi_compiler_location.named_child("Namespace"));
      
      /// \todo Need to add default implementations of Movable and Copyable for builtin types
      movable_interface = make_movable_copyable_interface(*this, true, psi_compiler_location.named_child("Movable"));
      copyable_interface = make_movable_copyable_interface(*this, false, psi_compiler_location.named_child("Copyable"));

      term_compile_argument = make_generic_type(empty_type, psi_compiler_location.named_child("TermCompileArgument"));
    }
  }
}
