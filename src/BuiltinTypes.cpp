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
      
      template<typename TreeType>
      TreePtr<MetadataType> make_tag(const TreePtr<Term>& wildcard_type, const SourceLocation& location, const TreePtr<>& default_value=TreePtr<>()) {
        PSI_STD::vector<TreePtr<Term> > pattern(1, TermBuilder::parameter(wildcard_type, 0, 0, location));
        
        PSI_STD::vector<TreePtr<Metadata> > values;
        if (default_value)
          values.push_back(Metadata::new_(default_value, default_, pattern.size(), pattern, location));
        
        return MetadataType::new_(wildcard_type->compile_context(), 0, pattern, values, reinterpret_cast<const SIVtable*>(&TreeType::vtable), location);
      }
      
      TreePtr<Metadata> make_default_macro(const TreePtr<Term>& tag, const TreePtr<>& value, bool tag_only=false) {
        PSI_STD::vector<TreePtr<Term> > pattern;
        if (!tag_only)
          pattern.push_back(TermBuilder::parameter(tag->compile_context().builtins().metatype, 0, 0, value->location()));
        pattern.push_back(tag);
        return Metadata::new_(value, default_, tag_only?0:1, pattern, value->location());
      }
      
      TreePtr<MetadataType> make_metadata_macro(CompileContext& compile_context, const SourceLocation& location) {
        PSI_STD::vector<TreePtr<Term> > pattern;
        pattern.push_back(TermBuilder::parameter(compile_context.builtins().metatype, 0, 0, location));
        pattern.push_back(TermBuilder::parameter(compile_context.builtins().metatype, 0, 1, location));
        
        PSI_STD::vector<TreePtr<Metadata> > values;
        values.push_back(make_default_macro(compile_context.builtins().macro_term_tag, default_macro_term(compile_context, location.named_child("TermDefault"))));
        values.push_back(make_default_macro(compile_context.builtins().macro_member_tag, default_macro_member(compile_context, location.named_child("AggregateMemberDefault"))));
        return MetadataType::new_(compile_context, 0, pattern, values, reinterpret_cast<const SIVtable*>(&Macro::vtable), location);
      }
      
      TreePtr<MetadataType> make_metadata_type_macro(CompileContext& compile_context, const SourceLocation& location) {
        PSI_STD::vector<TreePtr<Term> > pattern;
        pattern.push_back(TermBuilder::parameter(compile_context.builtins().metatype, 0, 0, location));
        pattern.push_back(TermBuilder::parameter(compile_context.builtins().metatype, 0, 1, location));
        
        PSI_STD::vector<TreePtr<Metadata> > values;
        values.push_back(make_default_macro(compile_context.builtins().macro_term_tag, default_type_macro_term(compile_context, location.named_child("TermDefault"))));
        values.push_back(make_default_macro(compile_context.builtins().macro_member_tag, default_type_macro_member(compile_context, location.named_child("AggregateMemberDefault"))));
        return MetadataType::new_(compile_context, 0, pattern, values, reinterpret_cast<const SIVtable*>(&Macro::vtable), location);
      }
      
      TreePtr<MetadataType> make_metadata_metatype_macro(CompileContext& compile_context, const SourceLocation& location) {
        PSI_STD::vector<TreePtr<Term> > pattern;
        pattern.push_back(TermBuilder::parameter(compile_context.builtins().metatype, 0, 0, location));
        
        PSI_STD::vector<TreePtr<Metadata> > values;
        values.push_back(make_default_macro(compile_context.builtins().macro_term_tag, default_type_macro_term(compile_context, location.named_child("TermDefault")), true));
        return MetadataType::new_(compile_context, 0, pattern, values, reinterpret_cast<const SIVtable*>(&Macro::vtable), location);
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
        
        return Interface::new_(0, vector_of<TreePtr<Term> >(builtins.metatype), default_, default_, exists, bases, location);
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

      i8_type = compile_context.get_functional(NumberType(NumberType::n_i8), psi_location.named_child("byte"));
      i16_type = compile_context.get_functional(NumberType(NumberType::n_i16), psi_location.named_child("short"));
      i32_type = compile_context.get_functional(NumberType(NumberType::n_i32), psi_location.named_child("int"));
      i64_type = compile_context.get_functional(NumberType(NumberType::n_i64), psi_location.named_child("long"));
      iptr_type = compile_context.get_functional(NumberType(NumberType::n_iptr), psi_location.named_child("size"));

      u8_type = compile_context.get_functional(NumberType(NumberType::n_u8), psi_location.named_child("ubyte"));
      u16_type = compile_context.get_functional(NumberType(NumberType::n_u16), psi_location.named_child("ushort"));
      u32_type = compile_context.get_functional(NumberType(NumberType::n_u32), psi_location.named_child("uint"));
      u64_type = compile_context.get_functional(NumberType(NumberType::n_u64), psi_location.named_child("ulong"));
      uptr_type = compile_context.get_functional(NumberType(NumberType::n_uptr), psi_location.named_child("usize"));
      
      macro_term_tag = make_generic_type(empty_type, psi_compiler_location.named_child("MacroTermTag"));
      macro_member_tag = make_generic_type(empty_type, psi_compiler_location.named_child("MacroMemberTag"));
      macro_interface_member_tag = make_generic_type(empty_type, psi_compiler_location.named_child("MacroInterfaceMemberTag"));
      macro_interface_definition_tag= make_generic_type(empty_type, psi_compiler_location.named_child("MacroInterfaceDefinitionTag"));

      macro = make_metadata_macro(compile_context, psi_compiler_location.named_child("Macro"));
      type_macro = make_metadata_type_macro(compile_context, psi_compiler_location.named_child("TypeMacro"));
      metatype_macro = make_metadata_metatype_macro(compile_context, psi_compiler_location.named_child("MetatypeMacro"));

      library_tag = make_tag<Library>(metatype, psi_compiler_location.named_child("Library"));
      namespace_tag = make_tag<Namespace>(metatype, psi_compiler_location.named_child("Namespace"));
      
      /// \todo Need to add default implementations of Movable and Copyable for builtin types
      movable_interface = make_movable_copyable_interface(*this, true, psi_compiler_location.named_child("Movable"));
      copyable_interface = make_movable_copyable_interface(*this, false, psi_compiler_location.named_child("Copyable"));
    }
  }
}
