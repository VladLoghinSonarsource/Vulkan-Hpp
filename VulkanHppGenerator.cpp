// Copyright(c) 2015-2020, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "VulkanHppGenerator.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <regex>
#include <sstream>

void                                        checkAttributes( int                                                  line,
                                                             std::map<std::string, std::string> const &           attributes,
                                                             std::map<std::string, std::set<std::string>> const & required,
                                                             std::map<std::string, std::set<std::string>> const & optional );
void                                        checkElements( int                                               line,
                                                           std::vector<tinyxml2::XMLElement const *> const & elements,
                                                           std::map<std::string, bool> const &               required,
                                                           std::set<std::string> const &                     optional = {} );
void                                        checkForError( bool condition, int line, std::string const & message );
void                                        checkForWarning( bool condition, int line, std::string const & message );
std::vector<std::pair<std::string, size_t>> filterNumbers( std::vector<std::string> const & names );
std::string                                 generateCArraySizes( std::vector<std::string> const & sizes );
std::string                                 generateNamespacedType( std::string const & type );
std::string                                 generateNoDiscard( bool returnsSomething, bool multiSuccessCodes, bool multiErrorCodes );
std::string                                 generateStandardArray( std::string const & type, std::vector<std::string> const & sizes );
std::string                                 generateStandardArrayWrapper( std::string const & type, std::vector<std::string> const & sizes );
std::map<std::string, std::string>          getAttributes( tinyxml2::XMLElement const * element );
template <typename ElementContainer>
std::vector<tinyxml2::XMLElement const *>        getChildElements( ElementContainer const * element );
bool                                             isNumber( std::string const & name );
std::pair<std::vector<std::string>, std::string> readModifiers( tinyxml2::XMLNode const * node );
std::string                                      readSnippet( std::string const & snippetFile );
std::string                                      replaceWithMap( std::string const & input, std::map<std::string, std::string> replacements );
std::string                                      startLowerCase( std::string const & input );
std::string                                      startUpperCase( std::string const & input );
std::string                                      stripPostfix( std::string const & value, std::string const & postfix );
std::string                                      stripPrefix( std::string const & value, std::string const & prefix );
std::string                                      toCamelCase( std::string const & value );
std::string                                      toUpperCase( std::string const & name );
std::vector<std::string>                         tokenize( std::string const & tokenString, std::string const & separator );
std::vector<std::string>                         tokenizeAny( std::string const & tokenString, std::string const & separators );
std::string                                      toString( tinyxml2::XMLError error );
std::string                                      trim( std::string const & input );
std::string                                      trimEnd( std::string const & input );
std::string                                      trimStars( std::string const & input );
void                                             writeToFile( std::string const & str, std::string const & fileName );

const std::set<std::string> specialPointerTypes = { "Display", "IDirectFB", "wl_display", "xcb_connection_t", "_screen_window" };

//
// VulkanHppGenerator public interface
//

VulkanHppGenerator::VulkanHppGenerator( tinyxml2::XMLDocument const & document, std::string const & api ) : m_api( api )
{
  // insert the default "handle" without class (for createInstance, and such)
  m_handles.insert( std::make_pair( "", HandleData() ) );

  // read the document and check its correctness
  int                                       line     = document.GetLineNum();
  std::vector<tinyxml2::XMLElement const *> elements = getChildElements( &document );
  checkElements( line, elements, { { "registry", true } } );
  checkForError( elements.size() == 1, line, "encountered " + std::to_string( elements.size() ) + " elements named <registry> but only one is allowed" );
  readRegistry( elements[0] );
  filterLenMembers();
  checkCorrectness();
  handleRemovals();

  // add the commands to the respective handles
  // some "FlagBits" enums are not specified, but needed for our "Flags" handling -> add them here
  for ( auto & feature : m_features )
  {
    addCommandsToHandle( feature.requireData );
    addMissingFlagBits( feature.requireData, feature.name );
  }
  for ( auto & extension : m_extensions )
  {
    addCommandsToHandle( extension.requireData );
    addMissingFlagBits( extension.requireData, extension.name );
  }
}

void VulkanHppGenerator::generateEnumsHppFile() const
{
  std::string const vulkan_enums_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_enums.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_enums_hpp << " ..." << std::endl;

  std::string const vulkanEnumsHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_ENUMS_HPP
#  define VULKAN_ENUMS_HPP

namespace VULKAN_HPP_NAMESPACE
{
  template <typename EnumType, EnumType value>
  struct CppType
  {};
${enums}
${indexTypeTraits}
}   // namespace VULKAN_HPP_NAMESPACE
#endif
)";

  std::string str = replaceWithMap(
    vulkanEnumsHppTemplate, { { "enums", generateEnums() }, { "indexTypeTraits", generateIndexTypeTraits() }, { "licenseHeader", m_vulkanLicenseHeader } } );

  writeToFile( str, vulkan_enums_hpp );
}

void VulkanHppGenerator::generateExtensionInspectionFile() const
{
  std::string const vulkan_extension_inspection_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_extension_inspection.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_extension_inspection_hpp << " ..." << std::endl;

  std::string const vulkanExtensionInspectionHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_EXTENSION_INSPECTION_HPP
#  define VULKAN_EXTENSION_INSPECTION_HPP

#include <vulkan/${api}.hpp>

namespace VULKAN_HPP_NAMESPACE
{
  //======================================
  //=== Extension inspection functions ===
  //======================================

  VULKAN_HPP_CONSTEXPR_20 std::string getExtensionDeprecatedBy( std::string const & name );
  VULKAN_HPP_CONSTEXPR_20 bool        isDeviceExtension( std::string const & name );
  VULKAN_HPP_CONSTEXPR_20 bool        isExtensionDeprecated( std::string const & name );
  VULKAN_HPP_CONSTEXPR_20 bool        isInstanceExtension( std::string const & name );

  //=====================================================
  //=== Extension inspection function implementations ===
  //=====================================================

  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_20 std::string getExtensionDeprecatedBy( std::string const & name )
  {
    ${voidName}
    ${deprecatedBy}
  }

  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_20 bool isDeviceExtension( std::string const & name )
  {
    return ${deviceTest};
  }

  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_20 bool isExtensionDeprecated( std::string const & name )
  {
    ${voidName}
    return ${deprecatedTest};
  }

  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_20 bool isInstanceExtension( std::string const & name )
  {
    return ${instanceTest};
  }
}   // namespace VULKAN_HPP_NAMESPACE

#endif
)";

  std::string str = replaceWithMap( vulkanExtensionInspectionHppTemplate,
                                    { { "api", m_api },
                                      { "deviceTest", generateExtensionTypeTest( "device" ) },
                                      { "deprecatedBy", generateExtensionDeprecatedBy() },
                                      { "deprecatedTest", generateExtensionDeprecatedTest() },
                                      { "instanceTest", generateExtensionTypeTest( "instance" ) },
                                      { "licenseHeader", m_vulkanLicenseHeader },
                                      { "voidName", ( m_api == "vulkan" ) ? "" : "(void)name;" } } );

  writeToFile( str, vulkan_extension_inspection_hpp );
}

void VulkanHppGenerator::generateFormatTraitsHppFile() const
{
  std::string const vulkan_format_traits_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_format_traits.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_format_traits_hpp << " ..." << std::endl;

  std::string const vulkanFormatTraitsHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_FORMAT_TRAITS_HPP
#  define VULKAN_FORMAT_TRAITS_HPP

#include <vulkan/vulkan.hpp>

namespace VULKAN_HPP_NAMESPACE
{
${formatTraits}
}   // namespace VULKAN_HPP_NAMESPACE
#endif
)";

  std::string str = replaceWithMap( vulkanFormatTraitsHppTemplate, { { "formatTraits", generateFormatTraits() }, { "licenseHeader", m_vulkanLicenseHeader } } );

  writeToFile( str, vulkan_format_traits_hpp );
}

void VulkanHppGenerator::generateFuncsHppFile() const
{
  std::string const vulkan_funcs_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_funcs.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_funcs_hpp << " ..." << std::endl;

  std::string const vulkanFuncsHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_FUNCS_HPP
#  define VULKAN_FUNCS_HPP

namespace VULKAN_HPP_NAMESPACE
{
${commandDefinitions}
}   // namespace VULKAN_HPP_NAMESPACE
#endif
)";

  std::string str =
    replaceWithMap( vulkanFuncsHppTemplate, { { "commandDefinitions", generateCommandDefinitions() }, { "licenseHeader", m_vulkanLicenseHeader } } );

  writeToFile( str, vulkan_funcs_hpp );
}

void VulkanHppGenerator::generateHandlesHppFile() const
{
  std::string const vulkan_handles_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_handles.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_handles_hpp << " ..." << std::endl;

  std::string const vulkanHandlesHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_HANDLES_HPP
#  define VULKAN_HANDLES_HPP

namespace VULKAN_HPP_NAMESPACE
{
${structForwardDeclarations}
${handleForwardDeclarations}
${uniqueHandles}
${handles}
}   // namespace VULKAN_HPP_NAMESPACE
#endif
)";

  std::string str = replaceWithMap( vulkanHandlesHppTemplate,
                                    { { "handles", generateHandles() },
                                      { "handleForwardDeclarations", generateHandleForwardDeclarations() },
                                      { "licenseHeader", m_vulkanLicenseHeader },
                                      { "structForwardDeclarations", generateStructForwardDeclarations() },
                                      { "uniqueHandles", generateUniqueHandles() } } );

  writeToFile( str, vulkan_handles_hpp );
}

void VulkanHppGenerator::generateHashHppFile() const
{
  std::string const vulkan_hash_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_hash.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_hash_hpp << " ..." << std::endl;

  std::string const vulkanHandlesHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_HASH_HPP
#  define VULKAN_HASH_HPP

#include <vulkan/vulkan.hpp>

namespace std
{
  //=======================================
  //=== HASH structures for Flags types ===
  //=======================================

  template <typename BitType>
  struct hash<VULKAN_HPP_NAMESPACE::Flags<BitType>>
  {
    std::size_t operator()( VULKAN_HPP_NAMESPACE::Flags<BitType> const & flags ) const VULKAN_HPP_NOEXCEPT
    {
      return std::hash<typename std::underlying_type<BitType>::type>{}(
        static_cast<typename std::underlying_type<BitType>::type>( flags ) );
    }
  };

${handleHashStructures}
${structHashStructures}
} // namespace std
#endif
)";

  std::string str = replaceWithMap( vulkanHandlesHppTemplate,
                                    { { "handleHashStructures", generateHandleHashStructures() },
                                      { "licenseHeader", m_vulkanLicenseHeader },
                                      { "structHashStructures", generateStructHashStructures() } } );

  writeToFile( str, vulkan_hash_hpp );
}

void VulkanHppGenerator::generateHppFile() const
{
  std::string const vulkan_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + ".hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_hpp << " ... " << std::endl;

  std::string const vulkanHppTemplate = R"(${licenseHeader}
${includes}

static_assert( VK_HEADER_VERSION == ${headerVersion}, "Wrong VK_HEADER_VERSION!" );

// 32-bit vulkan is not typesafe for non-dispatchable handles, so don't allow copy constructors on this platform by default.
// To enable this feature on 32-bit platforms please define VULKAN_HPP_TYPESAFE_CONVERSION
${typesafeCheck}
#  if !defined( VULKAN_HPP_TYPESAFE_CONVERSION )
#    define VULKAN_HPP_TYPESAFE_CONVERSION
#  endif
#endif

${defines}

namespace VULKAN_HPP_NAMESPACE
{
${ArrayWrapper1D}
${ArrayWrapper2D}
${Flags}
#if !defined( VULKAN_HPP_DISABLE_ENHANCED_MODE )
${ArrayProxy}
${ArrayProxyNoTemporaries}
${StridedArrayProxy}
${Optional}
${StructureChain}
${UniqueHandle}
#endif  // VULKAN_HPP_DISABLE_ENHANCED_MODE

${DispatchLoaderBase}
${DispatchLoaderStatic}
${DispatchLoaderDefault}
#if !defined( VULKAN_HPP_NO_SMART_HANDLE )
${ObjectDestroy}
${ObjectFree}
${ObjectRelease}
${PoolFree}
#endif // !VULKAN_HPP_NO_SMART_HANDLE
${baseTypes}
} // namespace VULKAN_HPP_NAMESPACE

#include <vulkan/vulkan_enums.hpp>
#if !defined( VULKAN_HPP_NO_TO_STRING )
#include <vulkan/vulkan_to_string.hpp>
#endif

#ifndef VULKAN_HPP_NO_EXCEPTIONS
namespace std
{
  template <>
  struct is_error_code_enum<VULKAN_HPP_NAMESPACE::Result> : public true_type
  {};
}  // namespace std
#endif

namespace VULKAN_HPP_NAMESPACE
{
#ifndef VULKAN_HPP_NO_EXCEPTIONS
${Exceptions}
${resultExceptions}
${throwResultException}
#endif

${ResultValue}
${resultChecks}
} // namespace VULKAN_HPP_NAMESPACE

// clang-format off
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <vulkan/vulkan_funcs.hpp>
// clang-format on

namespace VULKAN_HPP_NAMESPACE
{
#if !defined( VULKAN_HPP_DISABLE_ENHANCED_MODE )
${structExtendsStructs}
#endif // VULKAN_HPP_DISABLE_ENHANCED_MODE

${DynamicLoader}
${DispatchLoaderDynamic}
}   // namespace VULKAN_HPP_NAMESPACE
#endif
)";

  std::string str =
    replaceWithMap( vulkanHppTemplate,
                    { { "ArrayProxy", readSnippet( "ArrayProxy.hpp" ) },
                      { "ArrayProxyNoTemporaries", readSnippet( "ArrayProxyNoTemporaries.hpp" ) },
                      { "ArrayWrapper1D", readSnippet( "ArrayWrapper1D.hpp" ) },
                      { "ArrayWrapper2D", readSnippet( "ArrayWrapper2D.hpp" ) },
                      { "baseTypes", generateBaseTypes() },
                      { "defines", readSnippet( "defines.hpp" ) },
                      { "DispatchLoaderBase", readSnippet( "DispatchLoaderBase.hpp" ) },
                      { "DispatchLoaderDefault", readSnippet( "DispatchLoaderDefault.hpp" ) },
                      { "DispatchLoaderDynamic", generateDispatchLoaderDynamic() },
                      { "DispatchLoaderStatic", generateDispatchLoaderStatic() },
                      { "DynamicLoader", readSnippet( "DynamicLoader.hpp" ) },
                      { "Exceptions", readSnippet( "Exceptions.hpp" ) },
                      { "Flags", readSnippet( "Flags.hpp" ) },
                      { "headerVersion", m_version },
                      { "includes",
                        replaceWithMap( readSnippet( "includes.hpp" ),
                                        { { "vulkan_h", ( m_api == "vulkan" ) ? "vulkan.h" : "vulkan_sc_core.h" }, { "vulkan_hpp", m_api + ".hpp" } } ) },
                      { "licenseHeader", m_vulkanLicenseHeader },
                      { "ObjectDestroy", readSnippet( "ObjectDestroy.hpp" ) },
                      { "ObjectFree", readSnippet( "ObjectFree.hpp" ) },
                      { "ObjectRelease", readSnippet( "ObjectRelease.hpp" ) },
                      { "Optional", readSnippet( "Optional.hpp" ) },
                      { "PoolFree", readSnippet( "PoolFree.hpp" ) },
                      { "resultChecks", readSnippet( "resultChecks.hpp" ) },
                      { "resultExceptions", generateResultExceptions() },
                      { "structExtendsStructs", generateStructExtendsStructs() },
                      { "ResultValue", readSnippet( "ResultValue.hpp" ) },
                      { "StridedArrayProxy", readSnippet( "StridedArrayProxy.hpp" ) },
                      { "StructureChain", readSnippet( "StructureChain.hpp" ) },
                      { "throwResultException", generateThrowResultException() },
                      { "typesafeCheck", m_typesafeCheck },
                      { "UniqueHandle", readSnippet( "UniqueHandle.hpp" ) } } );

  writeToFile( str, vulkan_hpp );
}

void VulkanHppGenerator::generateRAIIHppFile() const
{
  std::string const vulkan_raii_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_raii.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_raii_hpp << " ..." << std::endl;

  std::string const vulkanHandlesHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_RAII_HPP
#define VULKAN_RAII_HPP

#include <memory>
#include <utility>  // std::exchange, std::forward
#include <vulkan/vulkan.hpp>

#if !defined( VULKAN_HPP_RAII_NAMESPACE )
#  define VULKAN_HPP_RAII_NAMESPACE raii
#endif

#if !defined( VULKAN_HPP_DISABLE_ENHANCED_MODE ) && !defined(VULKAN_HPP_NO_EXCEPTIONS)
namespace VULKAN_HPP_NAMESPACE
{
  namespace VULKAN_HPP_RAII_NAMESPACE
  {
    template <class T, class U = T>
    VULKAN_HPP_CONSTEXPR_14 VULKAN_HPP_INLINE T exchange( T & obj, U && newValue )
    {
#  if ( 14 <= VULKAN_HPP_CPP_VERSION )
      return std::exchange<T>( obj, std::forward<U>( newValue ) );
#  else
      T oldValue = std::move( obj );
      obj        = std::forward<U>( newValue );
      return oldValue;
#  endif
    }

${RAIIDispatchers}
${RAIIHandles}
${RAIICommandDefinitions}
  } // namespace VULKAN_HPP_RAII_NAMESPACE
}   // namespace VULKAN_HPP_NAMESPACE
#endif
#endif
)";

  std::string str = replaceWithMap( vulkanHandlesHppTemplate,
                                    { { "licenseHeader", m_vulkanLicenseHeader },
                                      { "RAIICommandDefinitions", generateRAIICommandDefinitions() },
                                      { "RAIIDispatchers", generateRAIIDispatchers() },
                                      { "RAIIHandles", generateRAIIHandles() } } );

  writeToFile( str, vulkan_raii_hpp );
}

void VulkanHppGenerator::generateStaticAssertionsHppFile() const
{
  std::string const static_assertions_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_static_assertions.hpp";
  std::cout << "VulkanHppGenerator: Generating " << static_assertions_hpp << " ..." << std::endl;

  std::string const vulkanHandlesHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_STATIC_ASSERTIONS_HPP
#  define VULKAN_STATIC_ASSERTIONS_HPP

#include <vulkan/vulkan.hpp>

//=========================
//=== static_assertions ===
//=========================

${staticAssertions}
#endif
)";

  std::string str =
    replaceWithMap( vulkanHandlesHppTemplate, { { "licenseHeader", m_vulkanLicenseHeader }, { "staticAssertions", generateStaticAssertions() } } );

  writeToFile( str, static_assertions_hpp );
}

void VulkanHppGenerator::generateStructsHppFile() const
{
  std::string const vulkan_structs_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_structs.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_structs_hpp << " ..." << std::endl;

  std::string const vulkanHandlesHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_STRUCTS_HPP
#  define VULKAN_STRUCTS_HPP

#include <cstring>  // strcmp

namespace VULKAN_HPP_NAMESPACE
{
${structs}
}   // namespace VULKAN_HPP_NAMESPACE
#endif
)";

  std::string str = replaceWithMap( vulkanHandlesHppTemplate, { { "licenseHeader", m_vulkanLicenseHeader }, { "structs", generateStructs() } } );

  writeToFile( str, vulkan_structs_hpp );
}

void VulkanHppGenerator::generateToStringHppFile() const
{
  std::string const vulkan_to_string_hpp = std::string( BASE_PATH ) + "/vulkan/" + m_api + "_to_string.hpp";
  std::cout << "VulkanHppGenerator: Generating " << vulkan_to_string_hpp << "..." << std::endl;

  std::string const vulkanHandlesHppTemplate = R"(${licenseHeader}
#ifndef VULKAN_TO_STRING_HPP
#  define VULKAN_TO_STRING_HPP

#include <vulkan/vulkan_enums.hpp>

#if __cpp_lib_format
#  include <format>   // std::format
#else
#  include <sstream>  // std::stringstream
#endif

namespace VULKAN_HPP_NAMESPACE
{
${bitmasksToString}
${enumsToString}
} // namespace VULKAN_HPP_NAMESPACE
#endif
)";

  std::string str = replaceWithMap(
    vulkanHandlesHppTemplate,
    { { "bitmasksToString", generateBitmasksToString() }, { "enumsToString", generateEnumsToString() }, { "licenseHeader", m_vulkanLicenseHeader } } );

  writeToFile( str, vulkan_to_string_hpp );
}

void VulkanHppGenerator::prepareRAIIHandles()
{
  // filter out functions that are not usefull on this level of abstraction (like vkGetInstanceProcAddr)
  // and all the destruction functions, as they are used differently
  assert( m_handles.begin()->first.empty() );
  for ( auto handleIt = std::next( m_handles.begin() ); handleIt != m_handles.end(); ++handleIt )
  {
    handleIt->second.destructorIt = determineRAIIHandleDestructor( handleIt->first );
    if ( handleIt->second.destructorIt != m_commands.end() )
    {
      m_RAIISpecialFunctions.insert( handleIt->second.destructorIt->first );
    }
    handleIt->second.constructorIts = determineRAIIHandleConstructors( handleIt->first, handleIt->second.destructorIt );
  }

  distributeSecondLevelCommands( m_RAIISpecialFunctions );
}

void VulkanHppGenerator::prepareVulkanFuncs()
{
  // rename a couple of function parameters to prevent this warning, treated as an error:
  // warning C4458: declaration of 'objectType' hides class member
  for ( auto & command : m_commands )
  {
    for ( auto & param : command.second.params )
    {
      if ( param.name == "objectType" )
      {
        param.name += "_";
      }
    }
  }
}

//
// VulkanHppGenerator private interface
//

void VulkanHppGenerator::addCommand( std::string const & name, CommandData & commandData )
{
  // find the handle this command is going to be associated to
  checkForError( !commandData.params.empty(), commandData.xmlLine, "command <" + name + "> with no params" );
  auto handleIt = m_handles.find( commandData.params[0].type.type );
  if ( handleIt == m_handles.end() )
  {
    handleIt = m_handles.begin();
    assert( handleIt->first == "" );
  }
  commandData.handle = handleIt->first;

  // add this command to the list of commands
  checkForError( m_commands.insert( std::make_pair( name, commandData ) ).second, commandData.xmlLine, "already encountered command <" + name + ">" );
}

void VulkanHppGenerator::addCommandsToHandle( std::vector<RequireData> const & requireData )
{
  for ( auto const & require : requireData )
  {
    for ( auto const & command : require.commands )
    {
      auto const & commandData = getCommandData( command );
      auto         handleIt    = m_handles.find( commandData.handle );
      assert( handleIt != m_handles.end() );
      if ( handleIt->second.commands.find( command ) == handleIt->second.commands.end() )
      {
        handleIt->second.commands.insert( command );
        registerDeleter( command, commandData );
      }
    }
  }
}

void VulkanHppGenerator::addMissingFlagBits( std::vector<RequireData> & requireData, std::string const & requiredBy )
{
  for ( auto & require : requireData )
  {
    std::vector<std::string> newTypes;
    for ( auto const & type : require.types )
    {
      auto bitmaskIt = m_bitmasks.find( type );
      if ( ( bitmaskIt != m_bitmasks.end() ) && bitmaskIt->second.require.empty() )
      {
        // generate the flagBits enum name out of the bitmask name: VkFooFlagsXXX -> VkFooFlagBitsXXX
        size_t pos = bitmaskIt->first.find( "Flags" );
        assert( pos != std::string::npos );
        std::string flagBits = bitmaskIt->first.substr( 0, pos + 4 ) + "Bit" + bitmaskIt->first.substr( pos + 4 );

        // as the bitmask's requirement is still empty, this flagBits should not be listed in the require list!
        assert( std::find_if( require.types.begin(), require.types.end(), [&flagBits]( std::string const & type ) { return ( type == flagBits ); } ) ==
                require.types.end() );

        bitmaskIt->second.require = flagBits;

        // some flagsBits are specified but never listed as required for any flags!
        // so, even if this bitmask has no enum listed as required, it might still already exist in the enums list
        auto enumIt = m_enums.find( flagBits );
        if ( enumIt == m_enums.end() )
        {
          m_enums.insert( std::make_pair( flagBits, EnumData{ .isBitmask = true, .xmlLine = 0 } ) );

          assert( m_types.find( flagBits ) == m_types.end() );
          m_types.insert( std::make_pair( flagBits, TypeData{ TypeCategory::Bitmask, { requiredBy }, 0 } ) );
        }
        else
        {
          assert( m_types.find( flagBits ) != m_types.end() );
          enumIt->second.isBitmask = true;
        }

        newTypes.push_back( flagBits );
      }
    }
    // add all the newly created flagBits types to the require list as if they had been part of the vk.xml!
    require.types.insert( require.types.end(), newTypes.begin(), newTypes.end() );
  }
}

std::string VulkanHppGenerator::addTitleAndProtection( std::string const & title, std::string const & strIf, std::string const & strElse ) const
{
  std::string str;
  if ( !strIf.empty() )
  {
    auto [enter, leave] = generateProtection( getProtectFromTitle( title ) );
    str                 = "\n" + enter + "  //=== " + title + " ===\n" + strIf;
    if ( !enter.empty() && !strElse.empty() )
    {
      str += "#else \n" + strElse;
    }
    str += leave;
  }
  return str;
}

bool VulkanHppGenerator::allVectorSizesSupported( std::vector<ParamData> const & params, std::map<size_t, VectorParamData> const & vectorParams ) const
{
  // check if all vector sizes are by value and their type is one of "uint32_t", "VkDeviceSize", or "VkSampleCountFlagBits"
  return std::find_if_not( vectorParams.begin(),
                           vectorParams.end(),
                           [&params]( auto const & vpi )
                           {
                             return params[vpi.second.lenParam].type.isValue() &&
                                    ( ( params[vpi.second.lenParam].type.type == "uint32_t" ) || ( params[vpi.second.lenParam].type.type == "VkDeviceSize" ) ||
                                      ( params[vpi.second.lenParam].type.type == "VkSampleCountFlagBits" ) );
                           } ) == vectorParams.end();
}

void VulkanHppGenerator::appendDispatchLoaderDynamicCommands( std::vector<RequireData> const & requireData,
                                                              std::set<std::string> &          listedCommands,
                                                              std::string const &              title,
                                                              std::string &                    commandMembers,
                                                              std::string &                    initialCommandAssignments,
                                                              std::string &                    instanceCommandAssignments,
                                                              std::string &                    deviceCommandAssignments ) const
{
  std::string members, initial, instance, device, placeholders;
  for ( auto const & require : requireData )
  {
    for ( auto const & command : require.commands )
    {
      if ( listedCommands.insert( command ).second )
      {
        auto const & commandData = getCommandData( command );

        members += "    PFN_" + command + " " + command + " = 0;\n";
        placeholders += "    PFN_dummy " + command + "_placeholder = 0;\n";
        if ( commandData.handle.empty() )
        {
          initial += generateDispatchLoaderDynamicCommandAssignment( command, "NULL" );
        }
        else
        {
          instance += generateDispatchLoaderDynamicCommandAssignment( command, "instance" );
          if ( isDeviceCommand( commandData ) )
          {
            device += generateDispatchLoaderDynamicCommandAssignment( command, "device" );
          }
        }
      }
    }
  }
  auto [enter, leave] = generateProtection( getProtectFromTitle( title ) );
  std::string header  = "\n" + enter + "  //=== " + title + " ===\n";
  if ( !members.empty() )
  {
    commandMembers += header + members;
    if ( !enter.empty() )
    {
      commandMembers += "#else\n" + placeholders;
    }
    commandMembers += leave;
  }
  if ( !initial.empty() )
  {
    initialCommandAssignments += header + initial + leave;
  }
  if ( !instance.empty() )
  {
    instanceCommandAssignments += header + instance + leave;
  }
  if ( !device.empty() )
  {
    deviceCommandAssignments += header + device + leave;
  }
}

void VulkanHppGenerator::appendRAIIDispatcherCommands( std::vector<RequireData> const & requireData,
                                                       std::set<std::string> &          listedCommands,
                                                       std::string const &              title,
                                                       std::string &                    contextInitializers,
                                                       std::string &                    contextMembers,
                                                       std::string &                    deviceAssignments,
                                                       std::string &                    deviceMembers,
                                                       std::string &                    instanceAssignments,
                                                       std::string &                    instanceMembers ) const
{
  std::string ci, cm, da, dm, dmp, ia, im, imp;
  for ( auto const & require : requireData )
  {
    for ( auto const & command : require.commands )
    {
      if ( listedCommands.insert( command ).second )
      {
        auto const & commandData = getCommandData( command );
        if ( commandData.handle.empty() )
        {
          assert( findAlias( command, m_commandAliases ) == m_commandAliases.end() );

          ci += ", " + command + "( PFN_" + command + "( getProcAddr( NULL, \"" + command + "\" ) ) )";

          cm += "      PFN_" + command + " " + command + " = 0;\n";
        }
        else if ( ( commandData.handle == "VkDevice" ) || hasParentHandle( commandData.handle, "VkDevice" ) )
        {
          da += "        " + command + " = PFN_" + command + "( vkGetDeviceProcAddr( device, \"" + command + "\" ) );\n";
          // if this is an alias'ed function, use it as a fallback for the original one
          auto aliasIt = m_commandAliases.find( command );
          if ( aliasIt != m_commandAliases.end() )
          {
            da += "        if ( !" + aliasIt->second.name + " ) " + aliasIt->second.name + " = " + command + ";\n";
          }

          dm += "      PFN_" + command + " " + command + " = 0;\n";
          dmp += "      PFN_dummy " + command + "_placeholder = 0;\n";
        }
        else
        {
          assert( ( commandData.handle == "VkInstance" ) || hasParentHandle( commandData.handle, "VkInstance" ) );

          // filter out vkGetInstanceProcAddr, as starting with Vulkan 1.2 it can resolve itself only (!) with an
          // instance nullptr !
          if ( command != "vkGetInstanceProcAddr" )
          {
            ia += "        " + command + " = PFN_" + command + "( vkGetInstanceProcAddr( instance, \"" + command + "\" ) );\n";
            // if this is an alias'ed function, use it as a fallback for the original one
            auto aliasIt = m_commandAliases.find( command );
            if ( aliasIt != m_commandAliases.end() )
            {
              ia += "        if ( !" + aliasIt->second.name + " ) " + aliasIt->second.name + " = " + command + ";\n";
            }
          }

          im += +"      PFN_" + command + " " + command + " = 0;\n";
          imp += "      PFN_dummy " + command + "_placeholder = 0;\n";
        }
      }
    }
  }
  contextInitializers += addTitleAndProtection( title, ci );
  contextMembers += addTitleAndProtection( title, cm );
  deviceAssignments += addTitleAndProtection( title, da );
  deviceMembers += addTitleAndProtection( title, dm, dmp );
  instanceAssignments += addTitleAndProtection( title, ia );
  instanceMembers += addTitleAndProtection( title, im, imp );
}

void VulkanHppGenerator::checkBitmaskCorrectness() const
{
  for ( auto const & bitmask : m_bitmasks )
  {
    // check that a bitmask is required somewhere
    // I think, it's not forbidden to not reference a bitmask, but it would probably be not intended?
    auto typeIt = m_types.find( bitmask.first );
    assert( typeIt != m_types.end() );
    checkForError( !typeIt->second.requiredBy.empty(), bitmask.second.xmlLine, "bitmask <" + bitmask.first + "> not required in any feature or extension" );

    // check that the requirement is an enum
    if ( !bitmask.second.require.empty() )
    {
      checkForError( m_enums.find( bitmask.second.require ) != m_enums.end(),
                     bitmask.second.xmlLine,
                     "bitmask <" + bitmask.first + "> requires unknown enum <" + bitmask.second.require + ">" );
    }
  }
}

void VulkanHppGenerator::checkCommandCorrectness() const
{
  // prepare command checks by gathering all result codes (including aliases and not supported ones!) into one set of resultCodes
  auto resultIt = m_enums.find( "VkResult" );
  assert( resultIt != m_enums.end() );
  std::set<std::string> resultCodes;
  for ( auto rc : resultIt->second.values )
  {
    resultCodes.insert( rc.name );
  }
  for ( auto rc : resultIt->second.unsupportedValues )
  {
    resultCodes.insert( rc.name );
  }

  // command checks
  for ( auto const & command : m_commands )
  {
    // check that a command is referenced somewhere
    // I think, it's not forbidden to not reference a function, but it would probably be not intended?
    checkForError( !command.second.requiredBy.empty(), command.second.xmlLine, "command <" + command.first + "> not required in any feature or extension" );

    // check for unknown error or succes codes
    for ( auto const & ec : command.second.errorCodes )
    {
      checkForError( resultCodes.find( ec ) != resultCodes.end(), command.second.xmlLine, "command uses unknown error code <" + ec + ">" );
    }
    for ( auto const & sc : command.second.successCodes )
    {
      checkForError( resultCodes.find( sc ) != resultCodes.end(), command.second.xmlLine, "command uses unknown success code <" + sc + ">" );
    }

    // check that functions returning a VkResult specify successcodes
    if ( ( command.second.returnType == "VkResult" ) && command.second.successCodes.empty() )
    {
      // emit an error if this function is required in at least one supported feature or extension
      // disabled or not supported features/extensions are still listed in requiredBy, but not in m_features/m_extensions
      bool functionUsed = false;
      for ( auto const & require : command.second.requiredBy )
      {
        functionUsed |= isSupportedFeature( require ) || isSupportedExtension( require );
      }
      if ( functionUsed )
      {
        checkForError( false, command.second.xmlLine, "missing successcodes on command <" + command.first + "> returning VkResult!" );
      }
    }

    // check that all parameter types as well as the return type are known types
    for ( auto const & p : command.second.params )
    {
      checkForError( m_types.find( p.type.type ) != m_types.end(), p.xmlLine, "comand uses parameter of unknown type <" + p.type.type + ">" );
    }
    checkForError( m_types.find( command.second.returnType ) != m_types.end(),
                   command.second.xmlLine,
                   "command uses unknown return type <" + command.second.returnType + ">" );
  }
}

void VulkanHppGenerator::checkCorrectness() const
{
  checkForError( !m_vulkanLicenseHeader.empty(), -1, "missing license header" );
  checkBitmaskCorrectness();
  checkCommandCorrectness();
  checkDefineCorrectness();
  checkEnumCorrectness();
  checkExtensionCorrectness();
  checkFuncPointerCorrectness();
  checkHandleCorrectness();
  checkStructCorrectness();
}

void VulkanHppGenerator::checkDefineCorrectness() const
{
  // check that any requirements of a define is known
  for ( auto const & d : m_defines )
  {
    checkForError( d.second.require.empty() || ( m_types.find( d.second.require ) != m_types.end() ),
                   d.second.xmlLine,
                   "define <" + d.first + "> uses unknown require <" + d.second.require + ">" );
  }
}

void VulkanHppGenerator::checkEnumCorrectness() const
{
  for ( auto const & e : m_enums )
  {
    // check that a bitmask is required somewhere
    // some bitmasks are never required, so make this a warning only
    auto typeIt = m_types.find( e.first );
    assert( typeIt != m_types.end() );
    checkForWarning( !typeIt->second.requiredBy.empty(), e.second.xmlLine, "enum <" + e.first + "> not required in any feature or extension" );

    // check that the alias names are known enum values
    for ( auto const & value : e.second.values )
    {
      checkForError( value.alias.empty() ||
                       ( std::find_if( e.second.values.begin(), e.second.values.end(), [&value]( auto const & evd ) { return evd.name == value.alias; } ) !=
                         e.second.values.end() ) ||
                       ( std::find_if( e.second.unsupportedValues.begin(),
                                       e.second.unsupportedValues.end(),
                                       [&value]( auto const & evd ) { return evd.name == value.alias; } ) != e.second.unsupportedValues.end() ),
                     value.xmlLine,
                     "enum <" + value.name + "> uses unknown alias <" + value.alias + ">" );
    }
  }

  // enum checks by features and extensions
  for ( auto & feature : m_features )
  {
    checkEnumCorrectness( feature.requireData );
  }
  for ( auto & ext : m_extensions )
  {
    checkEnumCorrectness( ext.requireData );
  }

  // special check for VkFormat
  if ( !m_formats.empty() )
  {
    auto enumIt = m_enums.find( "VkFormat" );
    assert( enumIt != m_enums.end() );
    assert( enumIt->second.values.front().name == "VK_FORMAT_UNDEFINED" );
    for ( auto enumValueIt = std::next( enumIt->second.values.begin() ); enumValueIt != enumIt->second.values.end(); ++enumValueIt )
    {
      // check for non-alias formats only
      checkForError( !enumValueIt->alias.empty() || ( m_formats.find( enumValueIt->name ) != m_formats.end() ),
                     enumValueIt->xmlLine,
                     "missing format specification for <" + enumValueIt->name + ">" );
    }
  }
}

void VulkanHppGenerator::checkEnumCorrectness( std::vector<RequireData> const & requireData ) const
{
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto typeIt = m_types.find( type );
      assert( typeIt != m_types.end() );
      switch ( typeIt->second.category )
      {
        case TypeCategory::Bitmask:
          {
            // check that each "require" listed for a bitmask is listed for a feature or an extension
            auto bitmaskIt = m_bitmasks.find( type );
            if ( bitmaskIt != m_bitmasks.end() )
            {
              // not for every bitmask is a "require" listed
              if ( !bitmaskIt->second.require.empty() )
              {
                auto requireTypeIt = m_types.find( bitmaskIt->second.require );
                assert( requireTypeIt != m_types.end() );
                checkForError( !requireTypeIt->second.requiredBy.empty(),
                               bitmaskIt->second.xmlLine,
                               "bitmask <" + bitmaskIt->first + "> requires <" + bitmaskIt->second.require +
                                 "> which is not required by any feature or extension!" );
              }
            }
            else
            {
              // every bitmask not listed in the m_bitmasks, should be an alias of such a thing
              checkForError(
                m_bitmaskAliases.find( type ) != m_bitmaskAliases.end(), typeIt->second.xmlLine, "bitmask type <" + type + "> is not listed as a bitmask" );
            }
          }
          break;
        case TypeCategory::Enum:
          {
            auto enumIt = m_enums.find( type );
            if ( enumIt != m_enums.end() )
            {
              if ( enumIt->second.isBitmask )
              {
                // check that any enum of a bitmask is listed as "require" or "bitvalues" for a bitmask
                auto bitmaskIt =
                  std::find_if( m_bitmasks.begin(), m_bitmasks.end(), [&enumIt]( auto const & bitmask ) { return bitmask.second.require == enumIt->first; } );
                checkForError( bitmaskIt != m_bitmasks.end(),
                               enumIt->second.xmlLine,
                               "enum <" + enumIt->first + "> is not listed as an requires or bitvalues for any bitmask in the types section" );

                // check that bitwidth of the enum and type of the corresponding bitmask are equal
                checkForError( ( enumIt->second.bitwidth != "64" ) || ( bitmaskIt->second.type == "VkFlags64" ),
                               enumIt->second.xmlLine,
                               "enum <" + enumIt->first + "> is marked with bitwidth <64> but corresponding bitmask <" + bitmaskIt->first +
                                 "> is not of type <VkFlags64>" );
              }
            }
            else
            {
              // every enum not listed in the m_enums, should be an alias of such a thing
              checkForError( m_enumAliases.find( type ) != m_enumAliases.end(), typeIt->second.xmlLine, "enum type <" + type + "> is not listed as an enum" );
            }
          }
          break;
        default: break;
      }
    }
  }
}

bool VulkanHppGenerator::checkEquivalentSingularConstructor( std::vector<std::map<std::string, CommandData>::const_iterator> const & constructorIts,
                                                             std::map<std::string, CommandData>::const_iterator                      constructorIt,
                                                             std::vector<ParamData>::const_iterator                                  lenIt ) const
{
  // check, if there is no singular constructor with the very same arguments as this array constructor
  // (besides the size, of course)
  auto isEquivalentSingularConstructor = [constructorIt, lenIt]( std::map<std::string, CommandData>::const_iterator it )
  {
    if ( it->second.params.size() + 1 != constructorIt->second.params.size() )
    {
      return false;
    }
    size_t lenIdx = std::distance( constructorIt->second.params.begin(), lenIt );
    for ( size_t i = 0, j = 0; i < it->second.params.size(); ++i, ++j )
    {
      assert( j < constructorIt->second.params.size() );
      if ( j == lenIdx )
      {
        ++j;
      }
      if ( it->second.params[i].type.type != constructorIt->second.params[j].type.type )
      {
        return false;
      }
    }
    return true;
  };
  return ( std::find_if( constructorIts.begin(), constructorIts.end(), isEquivalentSingularConstructor ) != constructorIts.end() );
}

void VulkanHppGenerator::checkExtensionCorrectness() const
{
  for ( auto const & extension : m_extensions )
  {
    // check for existence of any depends, deprecation, obsoletion, or promotion
    for ( auto const & depends : extension.depends )
    {
      checkForError(
        isFeature( depends ) || isExtension( depends ), extension.xmlLine, "extension <" + extension.name + "> lists an unknown depends <" + depends + ">" );
    }
    if ( !extension.deprecatedBy.empty() )
    {
      checkForError( isFeature( extension.deprecatedBy ) || isExtension( extension.deprecatedBy ),
                     extension.xmlLine,
                     "extension <" + extension.name + "> is deprecated by unknown extension/version <" + extension.promotedTo + ">" );
    }
    if ( !extension.obsoletedBy.empty() )
    {
      checkForError( isFeature( extension.obsoletedBy ) || isExtension( extension.obsoletedBy ),
                     extension.xmlLine,
                     "extension <" + extension.name + "> is obsoleted by unknown extension/version <" + extension.promotedTo + ">" );
    }
    if ( !extension.promotedTo.empty() )
    {
      checkForError( isFeature( extension.promotedTo ) || isExtension( extension.promotedTo ),
                     extension.xmlLine,
                     "extension <" + extension.name + "> is promoted to unknown extension/version <" + extension.promotedTo + ">" );
    }

    // check for existence of any requirement
    for ( auto const & require : extension.requireData )
    {
      if ( !require.depends.empty() )
      {
        for ( auto const & depends : require.depends )
        {
          checkForError(
            isFeature( depends ) || isExtension( depends ), require.xmlLine, "extension <" + extension.name + "> lists an unknown depends <" + depends + ">" );
        }
      }
    }
  }
}

void VulkanHppGenerator::checkFuncPointerCorrectness() const
{
  for ( auto const & funcPointer : m_funcPointers )
  {
    if ( !funcPointer.second.require.empty() )
    {
      checkForError( m_types.find( funcPointer.second.require ) != m_types.end(),
                     funcPointer.second.xmlLine,
                     "funcpointer requires unknown <" + funcPointer.second.require + ">" );
    }
    for ( auto const & argument : funcPointer.second.arguments )
    {
      checkForError( m_types.find( argument.type ) != m_types.end(), argument.xmlLine, "funcpointer argument of unknown type <" + argument.type + ">" );
    }
  }
}

void VulkanHppGenerator::checkHandleCorrectness() const
{
  // prepare handle checks by getting the VkObjectType enum
  auto objectTypeIt = m_enums.find( "VkObjectType" );
  assert( objectTypeIt != m_enums.end() );

  // handle checks
  for ( auto const & handle : m_handles )
  {
    // check the existence of the parent
    checkForError( m_handles.find( handle.second.parent ) != m_handles.end(),
                   handle.second.xmlLine,
                   "handle <" + handle.first + "> with unknown parent <" + handle.second.parent + ">" );

    // check existence of objTypeEnum used with this handle type
    if ( !handle.first.empty() )
    {
      assert( !handle.second.objTypeEnum.empty() );

      // only check with used handles!
      checkForError( !isTypeUsed( handle.first ) || std::find_if( objectTypeIt->second.values.begin(),
                                                                  objectTypeIt->second.values.end(),
                                                                  [&handle]( EnumValueData const & evd )
                                                                  { return evd.name == handle.second.objTypeEnum; } ) != objectTypeIt->second.values.end(),
                     handle.second.xmlLine,
                     "handle <" + handle.first + "> specifies unknown \"objtypeenum\" <" + handle.second.objTypeEnum + ">" );
    }
  }

  // check that all specified objectType values are used with a handle type
  for ( auto const & objectTypeValue : objectTypeIt->second.values )
  {
    if ( objectTypeValue.name != "VK_OBJECT_TYPE_UNKNOWN" )
    {
      // check for non-alias objectTypes only
      checkForError( !objectTypeValue.alias.empty() || ( std::find_if( m_handles.begin(),
                                                                       m_handles.end(),
                                                                       [&objectTypeValue]( std::pair<std::string, HandleData> const & hd )
                                                                       { return hd.second.objTypeEnum == objectTypeValue.name; } ) != m_handles.end() ),
                     objectTypeValue.xmlLine,
                     "VkObjectType value <" + objectTypeValue.name + "> not specified as \"objtypeenum\" for any handle" );
    }
  }
}

void VulkanHppGenerator::checkStructCorrectness() const
{
  for ( auto const & structAlias : m_structAliases )
  {
    checkForError(
      m_structs.find( structAlias.second.name ) != m_structs.end(), structAlias.second.xmlLine, "unknown struct alias <" + structAlias.second.name + ">" );
  }

  std::set<std::string> sTypeValues;
  for ( auto const & structure : m_structs )
  {
    // check that a struct is referenced somewhere
    // I think, it's not forbidden to not reference a struct, but it would probably be not intended?
    auto typeIt = m_types.find( structure.first );
    assert( typeIt != m_types.end() );
    checkForError(
      !typeIt->second.requiredBy.empty(), structure.second.xmlLine, "structure <" + structure.first + "> not required by any feature or extension" );

    // check for existence of all structs that are extended by this struct
    for ( auto const & extend : structure.second.structExtends )
    {
      checkForError( ( m_structs.find( extend ) != m_structs.end() ) || ( m_structAliases.find( extend ) != m_structAliases.end() ),
                     structure.second.xmlLine,
                     "struct <" + structure.first + "> extends unknown <" + extend + ">" );
    }

    // checks on the members of a struct
    checkStructMemberCorrectness( structure.first, structure.second.members, sTypeValues );
  }

  // enum VkStructureType checks (need to be after structure checks because of sTypeValues gathered there)
  auto structureTypeIt = m_enums.find( "VkStructureType" );
  assert( structureTypeIt != m_enums.end() );
  static std::set<std::string> reservedValues = { "VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO",
                                                  "VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO",
                                                  "VK_STRUCTURE_TYPE_PRIVATE_VENDOR_INFO_RESERVED_OFFSET_0_NV" };
  for ( auto const & enumValue : structureTypeIt->second.values )
  {
    if ( reservedValues.find( enumValue.name ) != reservedValues.end() )
    {
      checkForError(
        sTypeValues.find( enumValue.name ) == sTypeValues.end(), enumValue.xmlLine, "Reserved VkStructureType enum value <" + enumValue.name + "> is used" );
    }
    else
    {
      // check for non-alias structureTypes only
      checkForError( !enumValue.alias.empty() || ( sTypeValues.erase( enumValue.name ) == 1 ),
                     enumValue.xmlLine,
                     "VkStructureType enum value <" + enumValue.name + "> never used" );
    }
  }
  assert( sTypeValues.empty() );
}

void VulkanHppGenerator::checkStructMemberCorrectness( std::string const &             structureName,
                                                       std::vector<MemberData> const & members,
                                                       std::set<std::string> &         sTypeValues ) const
{
  // determine if this struct is requird/used
  bool structUsed = isTypeUsed( structureName );

  for ( auto const & member : members )
  {
    // check that all member types are required in some feature or extension
    if ( member.type.type.starts_with( "Vk" ) )
    {
      auto memberTypeIt = m_types.find( member.type.type );
      assert( memberTypeIt != m_types.end() );
      checkForError( !memberTypeIt->second.requiredBy.empty(),
                     member.xmlLine,
                     "struct member type <" + member.type.type + "> used in struct <" + structureName + "> is never required for any feature or extension" );
    }

    // if a member specifies a selector, that member is a union and the selector is an enum
    // check that there's a 1-1 connection between the specified selections and the values of that enum
    if ( !member.selector.empty() )
    {
      auto selectorIt = findStructMemberIt( member.selector, members );
      assert( selectorIt != members.end() );
      auto selectorEnumIt = m_enums.find( selectorIt->type.type );
      assert( selectorEnumIt != m_enums.end() );
      auto unionIt = m_structs.find( member.type.type );
      assert( ( unionIt != m_structs.end() ) && unionIt->second.isUnion );
      for ( auto const & unionMember : unionIt->second.members )
      {
        // check that each union member has a selection, that is a value of the seleting enum
        assert( !unionMember.selection.empty() );
        for ( auto const & selection : unionMember.selection )
        {
          checkForError(
            ( std::find_if( selectorEnumIt->second.values.begin(),
                            selectorEnumIt->second.values.end(),
                            [&selection]( EnumValueData const & evd ) { return evd.name == selection; } ) != selectorEnumIt->second.values.end() ) ||
              ( std::find_if( selectorEnumIt->second.unsupportedValues.begin(),
                              selectorEnumIt->second.unsupportedValues.end(),
                              [&selection]( EnumValueData const & evd ) { return evd.name == selection; } ) != selectorEnumIt->second.unsupportedValues.end() ),
            unionMember.xmlLine,
            "union member <" + unionMember.name + "> uses selection <" + selection + "> that is not part of the selector type <" + selectorIt->type.type +
              ">" );
        }
      }
    }

    // check that each member type is known
    checkForError( m_types.find( member.type.type ) != m_types.end(), member.xmlLine, "struct member uses unknown type <" + member.type.type + ">" );

    // check that any used constant is a known constant
    if ( !member.usedConstant.empty() )
    {
      checkForError( m_constants.find( member.usedConstant ) != m_constants.end(),
                     member.xmlLine,
                     "struct member array size uses unknown constant <" + member.usedConstant + ">" );
    }

    // checks if a value is specified
    if ( !member.value.empty() )
    {
      auto enumIt = m_enums.find( member.type.type );
      if ( enumIt != m_enums.end() )
      {
        // check that the value exists in the specified enum (if the struct is used at all)
        if ( structUsed )
        {
          checkForError( std::find_if( enumIt->second.values.begin(),
                                       enumIt->second.values.end(),
                                       [&member]( auto const & evd ) { return member.value == evd.name; } ) != enumIt->second.values.end(),
                         member.xmlLine,
                         "value <" + member.value + "> for member <" + member.name + "> in structure <" + structureName + "> of enum type <" +
                           member.type.type + "> not listed" );
          // special handling for sType: no value should appear more than once
          if ( member.name == "sType" )
          {
            checkForError( sTypeValues.insert( member.value ).second, member.xmlLine, "sType value <" + member.value + "> has been used before" );
          }
        }
      }
      else if ( member.type.type == "uint32_t" )
      {
        // check that a value for a uint32_t is all digits
        checkForError( member.value.find_first_not_of( "0123456789" ) == std::string::npos,
                       member.xmlLine,
                       "value <" + member.value + "> for member <" + member.name + "> in structure <" + structureName + "> of type <" + member.type.type +
                         "> is not a number" );
      }
      else
      {
        // don't know the type of the value -> error out
        checkForError( false,
                       member.xmlLine,
                       "member <" + member.name + "> in structure <" + structureName + "> holds value <" + member.value + "> for an unhandled type <" +
                         member.type.type + ">" );
      }
    }
  }
}

std::string VulkanHppGenerator::combineDataTypes( std::map<size_t, VectorParamData> const & vectorParams,
                                                  std::vector<size_t> const &               returnParams,
                                                  bool                                      enumerating,
                                                  std::vector<std::string> const &          dataTypes,
                                                  CommandFlavourFlags                       flavourFlags,
                                                  bool                                      raii ) const
{
  assert( dataTypes.size() == returnParams.size() );

  std::vector<std::string> modifiedDataTypes( dataTypes.size() );
  for ( size_t i = 0; i < returnParams.size(); ++i )
  {
    auto vectorParamIt   = vectorParams.find( returnParams[i] );
    modifiedDataTypes[i] = ( vectorParamIt == vectorParams.end() || ( flavourFlags & CommandFlavourFlagBits::singular ) )
                           ? dataTypes[i]
                           : ( "std::vector<" + dataTypes[i] +
                               ( raii || ( flavourFlags & CommandFlavourFlagBits::unique )
                                   ? ">"
                                   : ( ", " + startUpperCase( stripPrefix( dataTypes[i], "VULKAN_HPP_NAMESPACE::" ) ) + "Allocator>" ) ) );
  }

  std::string combinedType;
  switch ( modifiedDataTypes.size() )
  {
    case 0: combinedType = "void"; break;
    case 1: combinedType = modifiedDataTypes[0]; break;
    case 2:
      assert( !enumerating || ( ( vectorParams.find( returnParams[1] ) != vectorParams.end() ) &&
                                ( vectorParams.find( returnParams[1] )->second.lenParam == returnParams[0] ) ) );
      combinedType = enumerating ? modifiedDataTypes[1] : ( "std::pair<" + modifiedDataTypes[0] + ", " + modifiedDataTypes[1] + ">" );
      break;
    case 3:
      assert( enumerating );
      switch ( vectorParams.size() )
      {
        case 1:
          assert( ( vectorParams.begin()->first == returnParams[2] ) && ( vectorParams.begin()->second.lenParam == returnParams[1] ) );
          combinedType = "std::pair<" + modifiedDataTypes[0] + ", " + modifiedDataTypes[2] + ">";
          break;
        case 2:
          assert( ( vectorParams.begin()->first == returnParams[1] ) && ( vectorParams.begin()->second.lenParam == returnParams[0] ) &&
                  ( std::next( vectorParams.begin() )->first == returnParams[2] ) &&
                  ( std::next( vectorParams.begin() )->second.lenParam == returnParams[0] ) );
          combinedType = "std::pair<" + modifiedDataTypes[1] + ", " + modifiedDataTypes[2] + ">";
          break;
        default: assert( false ); break;
      }
      break;
    default: assert( false ); break;
  }
  return combinedType;
}

bool VulkanHppGenerator::containsArray( std::string const & type ) const
{
  // a simple recursive check if a type is or contains an array
  auto structureIt = m_structs.find( type );
  bool found       = false;
  if ( structureIt != m_structs.end() )
  {
    for ( auto memberIt = structureIt->second.members.begin(); memberIt != structureIt->second.members.end() && !found; ++memberIt )
    {
      found = !memberIt->arraySizes.empty() || containsArray( memberIt->type.type );
    }
  }
  return found;
}

bool VulkanHppGenerator::containsFuncPointer( std::string const & type ) const
{
  // a simple recursive check if a type contains a funcpointer
  auto structureIt = m_structs.find( type );
  bool found       = false;
  if ( structureIt != m_structs.end() )
  {
    for ( auto memberIt = structureIt->second.members.begin(); memberIt != structureIt->second.members.end() && !found; ++memberIt )
    {
      found = ( m_funcPointers.find( memberIt->type.type ) != m_funcPointers.end() ) ||
              ( ( memberIt->type.type != type ) && containsFuncPointer( memberIt->type.type ) );
    }
  }
  return found;
}

bool VulkanHppGenerator::containsFloatingPoints( std::vector<MemberData> const & members ) const
{
  for ( auto const & m : members )
  {
    if ( ( ( m.type.type == "float" ) || ( m.type.type == "double" ) ) && m.type.isValue() )
    {
      return true;
    }
  }
  return false;
}

bool VulkanHppGenerator::containsUnion( std::string const & type ) const
{
  // a simple recursive check if a type is or contains a union
  auto structureIt = m_structs.find( type );
  bool found       = false;
  if ( structureIt != m_structs.end() )
  {
    found = structureIt->second.isUnion;
    for ( auto memberIt = structureIt->second.members.begin(); memberIt != structureIt->second.members.end() && !found; ++memberIt )
    {
      found = memberIt->type.isValue() && containsUnion( memberIt->type.type );
    }
  }
  return found;
}

std::vector<size_t> VulkanHppGenerator::determineConstPointerParams( std::vector<ParamData> const & params ) const
{
  std::vector<size_t> constPointerParams;

  for ( size_t i = 0; i < params.size(); i++ )
  {
    // very special handling for some types, which come in as non-const pointers, but are meant as const-pointers
    if ( params[i].type.isConstPointer() ||
         ( params[i].type.isNonConstPointer() && ( specialPointerTypes.find( params[i].type.type ) != specialPointerTypes.end() ) ) )
    {
      constPointerParams.push_back( i );
    }
  }
  return constPointerParams;
}

std::vector<std::string> VulkanHppGenerator::determineDataTypes( std::vector<VulkanHppGenerator::ParamData> const & params,
                                                                 std::map<size_t, VectorParamData> const &          vectorParams,
                                                                 std::vector<size_t> const &                        returnParams,
                                                                 std::set<size_t> const &                           templatedParams ) const
{
  std::vector<std::string> dataTypes;
  for ( auto rp : returnParams )
  {
    if ( templatedParams.find( rp ) != templatedParams.end() )
    {
      auto vectorParamIt = vectorParams.find( rp );
      if ( ( vectorParamIt != vectorParams.end() ) && ( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->first ) != returnParams.end() ) &&
           ( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->second.lenParam ) != returnParams.end() ) )
      {
        dataTypes.push_back( "uint8_t" );
      }
      else
      {
        dataTypes.push_back( ( stripPrefix( params[rp].name, "p" ) + "Type" ) );
      }
    }
    else
    {
      dataTypes.push_back( trimEnd( stripPostfix( params[rp].type.compose( "VULKAN_HPP_NAMESPACE" ), "*" ) ) );
    }
  }
  return dataTypes;
}

size_t VulkanHppGenerator::determineDefaultStartIndex( std::vector<ParamData> const & params, std::set<size_t> const & skippedParams ) const
{
  // determine the index where the arguments start to have defaults
  size_t defaultStartIndex = INVALID_INDEX;
  for ( int i = static_cast<int>( params.size() ) - 1; ( 0 <= i ) && ( params[i].optional || ( skippedParams.find( i ) != skippedParams.end() ) ); --i )
  {
    defaultStartIndex = i;
  }
  return defaultStartIndex;
}

bool VulkanHppGenerator::determineEnumeration( std::map<size_t, VectorParamData> const & vectorParams, std::vector<size_t> const & returnParams ) const
{
  // a command is considered to be enumerating some data, if for at least one vectorParam both, the data and the counter, are returnParams
  return std::find_if( vectorParams.begin(),
                       vectorParams.end(),
                       [&returnParams]( auto const & vp )
                       {
                         return std::find( returnParams.begin(), returnParams.end(), vp.first ) != returnParams.end() &&
                                std::find( returnParams.begin(), returnParams.end(), vp.second.lenParam ) != returnParams.end();
                       } ) != vectorParams.end();
}

size_t VulkanHppGenerator::determineInitialSkipCount( std::string const & command ) const
{
  // determine the number of arguments to skip for a function
  // -> 0: the command is not bound to an instance or a device (the corresponding handle has no name)
  // -> 1: the command bound to an instance or a device (the corresponding handle has a name)
  // -> 2: the command has been moved to a second handle
  auto commandIt = m_commands.find( command );
  if ( commandIt == m_commands.end() )
  {
    auto aliasIt = m_commandAliases.find( command );
    assert( aliasIt != m_commandAliases.end() );
    commandIt = m_commands.find( aliasIt->second.name );
    assert( commandIt != m_commands.end() );
  }
  auto handleIt = m_handles.find( commandIt->second.handle );
  assert( handleIt != m_handles.end() );
  if ( handleIt->second.commands.find( commandIt->first ) == handleIt->second.commands.end() )
  {
    assert( 1 < commandIt->second.params.size() );
    assert( m_handles.find( commandIt->second.params[1].type.type ) != m_handles.end() );
    return 2;
  }
  else
  {
    return handleIt->first.empty() ? 0 : 1;
  }
}

std::vector<size_t> VulkanHppGenerator::determineReturnParams( std::vector<ParamData> const & params ) const
{
  std::vector<size_t> nonConstPointerParamIndices;

  for ( size_t i = 0; i < params.size(); i++ )
  {
    // very special handling of parameters of some types, which always come as a non-const pointer but are not meant
    // to be a potential return value!
    if ( params[i].type.isNonConstPointer() && ( specialPointerTypes.find( params[i].type.type ) == specialPointerTypes.end() ) )
    {
      nonConstPointerParamIndices.push_back( i );
    }
  }
  return nonConstPointerParamIndices;
}

std::vector<std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator>
  VulkanHppGenerator::determineRAIIHandleConstructors( std::string const & handleType, std::map<std::string, CommandData>::const_iterator destructorIt ) const
{
  std::vector<std::map<std::string, CommandData>::const_iterator> constructorIts;
  auto                                                            isConstructorCandidate = [this, &handleType]( std::pair<std::string, CommandData> const & cd )
  {
    return isSupported( cd.second.requiredBy ) && ( std::find_if( cd.second.params.begin(),
                                                                  cd.second.params.end(),
                                                                  [&handleType]( ParamData const & pd ) {
                                                                    return ( pd.type.type == handleType ) && pd.type.isNonConstPointer();
                                                                  } ) != cd.second.params.end() );
  };
  for ( auto commandIt = m_commands.begin(); commandIt != m_commands.end(); )
  {
    // find the commands that get a non-const pointer to the handleType, that is, return a handle type
    commandIt = std::find_if( commandIt, m_commands.end(), isConstructorCandidate );
    if ( commandIt != m_commands.end() )
    {
      // only commands that provide all information needed for the destructor can be considered a constructor!
      bool valid = true;
      if ( destructorIt != m_commands.end() )
      {
        // get the destructors parameter to the handleType
        auto desctructorHandleParamIt = std::find_if( destructorIt->second.params.begin(),
                                                      destructorIt->second.params.end(),
                                                      [&handleType]( ParamData const & pd ) { return pd.type.type == handleType; } );
        assert( desctructorHandleParamIt != destructorIt->second.params.end() );

        // lambda to check if a destructor parameter is a parameter of the constructor candidate
        // (or it's just the len parameter, which is not needed for the constructor)
        auto isConstructorCandidateParam = [&desctructorHandleParamIt, &commandIt, this]( ParamData const & destructorParam )
        {
          // check if the destructor param type equals this param type, or, if this param type is a struct, is part of
          // that struct
          auto isDestructorParamType = [&destructorParam, this]( ParamData const & pd )
          {
            if ( pd.type.type != destructorParam.type.type )
            {
              // check if the destructor param type equals a structure member type
              auto structureIt = m_structs.find( pd.type.type );
              return ( structureIt != m_structs.end() ) &&
                     ( findStructMemberItByType( destructorParam.type.type, structureIt->second.members ) != structureIt->second.members.end() );
            }
            return true;
          };

          return ( destructorParam.name == desctructorHandleParamIt->lenExpression ) ||
                 ( std::find_if( commandIt->second.params.begin(), commandIt->second.params.end(), isDestructorParamType ) != commandIt->second.params.end() );
        };

        // the constructor candidate is valid, if none of the (relevant) destructor parameters is missing in the
        // constructor candidate params
        valid = ( std::find_if_not( destructorIt->second.params.begin(), destructorIt->second.params.end(), isConstructorCandidateParam ) ==
                  destructorIt->second.params.end() );
      }
      if ( valid )
      {
        constructorIts.push_back( commandIt );
      }
      ++commandIt;
    }
  }
  return constructorIts;
}

std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator VulkanHppGenerator::determineRAIIHandleDestructor( std::string const & handleType ) const
{
  std::string type         = stripPrefix( handleType, "Vk" );
  auto        destructorIt = m_commands.find( "vkDestroy" + type );
  if ( destructorIt == m_commands.end() )
  {
    destructorIt = m_commands.find( "vkFree" + type + "s" );
    if ( destructorIt == m_commands.end() )
    {
      destructorIt = m_commands.find( "vkRelease" + type );
      if ( destructorIt == m_commands.end() )
      {
        if ( handleType == "VkDeviceMemory" )
        {
          // special handling for vkDeviceMemory
          destructorIt = m_commands.find( "vkFreeMemory" );
          assert( destructorIt != m_commands.end() );
        }
        else if ( handleType == "VkDisplayKHR" )
        {
          // special handling for VkDisplayKHR
          destructorIt = m_commands.find( "vkReleaseDisplayEXT" );
          assert( destructorIt != m_commands.end() );
        }
        else
        {
          assert( ( handleType == "VkDisplayModeKHR" ) || ( handleType == "VkPhysicalDevice" ) || ( handleType == "VkQueue" ) );
        }
      }
    }
  }
  return destructorIt;
}

std::set<size_t> VulkanHppGenerator::determineSingularParams( size_t returnParam, std::map<size_t, VectorParamData> const & vectorParams ) const
{
  auto returnVectorIt = vectorParams.find( returnParam );
  assert( returnVectorIt != vectorParams.end() );
  std::set<size_t> singularParams;
  singularParams.insert( returnVectorIt->second.lenParam );
  for ( auto const & vpi : vectorParams )
  {
    if ( vpi.second.lenParam == returnVectorIt->second.lenParam )
    {
      singularParams.insert( vpi.first );
    }
  }
  return singularParams;
}

std::set<size_t> VulkanHppGenerator::determineSkippedParams( std::vector<ParamData> const &            params,
                                                             size_t                                    initialSkipCount,
                                                             std::map<size_t, VectorParamData> const & vectorParams,
                                                             std::vector<size_t> const &               returnParams,
                                                             bool                                      singular ) const
{
  // skip the initial skips (get fed by the object)
  assert( initialSkipCount <= params.size() );
  std::set<size_t> skippedParams;
  for ( size_t i = 0; i < initialSkipCount; ++i )
  {
    skippedParams.insert( i );
  }

  // skip the size parameters (get derived from an array), and a stride parameter
  for ( auto const & vpi : vectorParams )
  {
    assert( !params[vpi.first].lenExpression.empty() );
    if ( ( ( std::find_if( returnParams.begin(), returnParams.end(), [&vpi]( size_t rpi ) { return vpi.first == rpi; } ) == returnParams.end() ) &&
           isParam( params[vpi.first].lenExpression, params ) ) ||
         ( singular && params[vpi.second.lenParam].type.isValue() ) )
    {
      skippedParams.insert( vpi.second.lenParam );
    }
    if ( vpi.second.strideParam != INVALID_INDEX )
    {
      skippedParams.insert( vpi.second.strideParam );
    }
  }

  // skip the return parameters (get resolved by local variables to be returned)
  skippedParams.insert( returnParams.begin(), returnParams.end() );

  return skippedParams;
}

std::string VulkanHppGenerator::determineSubStruct( std::pair<std::string, StructureData> const & structure ) const
{
  if ( structure.second.members.front().name != "sType" )
  {
    // check if sd is a substruct of structure
    auto isSubStruct = [&structure]( std::pair<std::string, StructureData> const & sd )
    {
      // member-by-member comparison of type and name
      auto memberIt = structure.second.members.begin();
      auto isMember = [&memberIt]( MemberData const & md )
      {
        if ( ( md.type == memberIt->type ) && ( md.name == memberIt->name ) )
        {
          ++memberIt;
          return true;
        }
        return false;
      };

      return ( sd.second.members.size() < structure.second.members.size() ) &&
             ( std::find_if_not( sd.second.members.begin(), sd.second.members.end(), isMember ) == sd.second.members.end() );
    };

    // look for a struct in m_structs that starts identically to structure
    auto structIt = std::find_if( m_structs.begin(), m_structs.end(), isSubStruct );
    return ( structIt == m_structs.end() ) ? "" : structIt->first;
  }
  return "";
}

std::map<size_t, VulkanHppGenerator::VectorParamData> VulkanHppGenerator::determineVectorParams( std::vector<ParamData> const & params ) const
{
  std::map<size_t, VectorParamData> vectorParams;

  // look for the parameters whose len equals the name of an other parameter
  for ( size_t i = 0; i < params.size(); i++ )
  {
    if ( !params[i].lenExpression.empty() && ( params[i].lenExpression != "null-terminated" ) )
    {
      VectorParamData & vpd = vectorParams[i];

      if ( params[i].lenParams.empty() )
      {
        std::string const & lenExpression = params[i].lenExpression;
        assert( std::find_if( params.begin(), params.end(), [&lenExpression]( auto const & pd ) { return ( lenExpression == pd.name ); } ) == params.end() );
        auto lenIt =
          std::find_if( params.begin(), params.end(), [this, &lenExpression]( auto const & pd ) { return isLenByStructMember( lenExpression, pd ); } );
        assert( lenIt != params.end() );
        vpd.lenParam = std::distance( params.begin(), lenIt );
      }
      else
      {
        assert( params[i].lenParams.size() == 1 );
        vpd.lenParam = params[i].lenParams[0].second;
      }
      if ( !params[i].strideParam.first.empty() )
      {
        vpd.strideParam = params[i].strideParam.second;
      }
    }
  }
  return vectorParams;
}

std::set<size_t> VulkanHppGenerator::determineVoidPointerParams( std::vector<ParamData> const & params ) const
{
  std::set<size_t> voidPointerParams;

  for ( size_t i = 0; i < params.size(); i++ )
  {
    if ( !params[i].type.isValue() && ( params[i].type.type == "void" ) && ( params[i].type.postfix != "**" ) )
    {
      voidPointerParams.insert( i );
    }
  }
  return voidPointerParams;
}

void VulkanHppGenerator::distributeSecondLevelCommands( std::set<std::string> const & specialFunctions )
{
  // distribute commands from instance/device to second-level handles, like Queue, Event,... for RAII handles
  for ( auto & handle : m_handles )
  {
    if ( !handle.first.empty() )
    {
      for ( auto command = handle.second.commands.begin(); command != handle.second.commands.end(); )
      {
        bool foundCommand = false;
        if ( specialFunctions.find( *command ) == specialFunctions.end() )
        {
          auto const & commandData = getCommandData( *command );
          assert( commandData.params.front().type.type == handle.first );
          if ( ( 1 < commandData.params.size() ) && ( isHandleType( commandData.params[1].type.type ) ) && !commandData.params[1].optional )
          {
            auto handleIt = m_handles.find( commandData.params[1].type.type );
            assert( handleIt != m_handles.end() );
            // filter out functions seem to fit due to taking handles as first and second argument, but the first argument is not the
            // type to create the second one, and so it's unknown to the raii handle!
            assert( !handleIt->second.constructorIts.empty() );
            if ( ( *handleIt->second.constructorIts.begin() )->second.handle == handle.first )
            {
              assert( std::find_if( handleIt->second.constructorIts.begin(),
                                    handleIt->second.constructorIts.end(),
                                    [&handle]( auto const & constructorIt )
                                    { return constructorIt->second.handle != handle.first; } ) == handleIt->second.constructorIts.end() );
              handleIt->second.secondLevelCommands.insert( *command );
              command      = handle.second.commands.erase( command );
              foundCommand = true;
            }
          }
        }
        if ( !foundCommand )
        {
          ++command;
        }
      }
    }
  }
}

void VulkanHppGenerator::filterLenMembers()
{
  for ( auto & sd : m_structs )
  {
    for ( auto & member : sd.second.members )
    {
      std::erase_if( member.lenMembers,
                     [this]( std::pair<std::string, size_t> const & nameIndex ) { return m_constants.find( nameIndex.first ) != m_constants.end(); } );

      for ( auto & lenMember : member.lenMembers )
      {
        if ( lenMember.second == size_t( ~0 ) )
        {
          auto lenMemberIt = findStructMemberIt( lenMember.first, sd.second.members );
          checkForError( lenMemberIt != sd.second.members.end(),
                         member.xmlLine,
                         "struct member <" + member.name + "> uses unknown len member <" + lenMember.first + "> in its \"altlen\" attribute <" +
                           member.lenExpressions[0] + ">" );
          lenMember.second = std::distance( sd.second.members.cbegin(), lenMemberIt );
        }
      }
    }
  }
}

std::map<std::string, VulkanHppGenerator::AliasData>::const_iterator VulkanHppGenerator::findAlias( std::string const &                      name,
                                                                                                    std::map<std::string, AliasData> const & aliases ) const
{
  auto lambda = [&name]( std::pair<std::string, AliasData> const & ad ) { return ad.second.name == name; };
  auto it     = std::find_if( aliases.begin(), aliases.end(), lambda );
  assert( ( it == aliases.end() ) || ( std::find_if( std::next( it ), aliases.end(), lambda ) == aliases.end() ) );
  return it;
}

std::string VulkanHppGenerator::findBaseName( std::string aliasName, std::map<std::string, AliasData> const & aliases ) const
{
  std::string baseName = aliasName;
  auto        aliasIt  = aliases.find( baseName );
  while ( aliasIt != aliases.end() )
  {
    baseName = aliasIt->second.name;
    aliasIt  = aliases.find( baseName );
  }
  return baseName;
}

std::vector<VulkanHppGenerator::FeatureData>::const_iterator VulkanHppGenerator::findFeature( std::string const & name ) const
{
  return std::find_if( m_features.begin(), m_features.end(), [&name]( FeatureData const & fd ) { return fd.name == name; } );
}

std::vector<VulkanHppGenerator::ParamData>::const_iterator VulkanHppGenerator::findParamIt( std::string const &            name,
                                                                                            std::vector<ParamData> const & paramData ) const
{
  return std::find_if( paramData.begin(), paramData.end(), [&name]( ParamData const & pd ) { return pd.name == name; } );
}

std::vector<VulkanHppGenerator::MemberData>::const_iterator VulkanHppGenerator::findStructMemberIt( std::string const &             name,
                                                                                                    std::vector<MemberData> const & memberData ) const
{
  return std::find_if( memberData.begin(), memberData.end(), [&name]( MemberData const & md ) { return md.name == name; } );
}

std::vector<VulkanHppGenerator::MemberData>::const_iterator VulkanHppGenerator::findStructMemberItByType( std::string const &             type,
                                                                                                          std::vector<MemberData> const & memberData ) const
{
  return std::find_if( memberData.begin(), memberData.end(), [&type]( MemberData const & md ) { return md.type.type == type; } );
}

std::vector<VulkanHppGenerator::ExtensionData>::const_iterator VulkanHppGenerator::findSupportedExtension( std::string const & name ) const
{
  return std::find_if( m_extensions.begin(), m_extensions.end(), [&name]( ExtensionData const & ed ) { return ed.name == name; } );
}

std::string VulkanHppGenerator::findTag( std::string const & name, std::string const & postfix ) const
{
  auto tagIt = std::find_if(
    m_tags.begin(), m_tags.end(), [&name, &postfix]( std::pair<std::string, TagData> const & t ) { return name.ends_with( t.first + postfix ); } );
  return ( tagIt != m_tags.end() ) ? tagIt->first : "";
}

std::pair<std::string, std::string> VulkanHppGenerator::generateAllocatorTemplates( std::vector<size_t> const &               returnParams,
                                                                                    std::vector<std::string> const &          returnDataTypes,
                                                                                    std::map<size_t, VectorParamData> const & vectorParams,
                                                                                    CommandFlavourFlags                       flavourFlags,
                                                                                    bool                                      definition ) const
{
  bool chained  = flavourFlags & CommandFlavourFlagBits::chained;
  bool singular = flavourFlags & CommandFlavourFlagBits::singular;
  bool unique   = flavourFlags & CommandFlavourFlagBits::unique;

  assert( returnParams.size() == returnDataTypes.size() );
  std::string allocatorTemplates;
  if ( !singular )
  {
    for ( size_t i = 0; i < returnParams.size(); i++ )
    {
      if ( vectorParams.find( returnParams[i] ) != vectorParams.end() )
      {
        if ( chained )
        {
          allocatorTemplates += "typename StructureChainAllocator";
          if ( !definition )
          {
            allocatorTemplates += " = std::allocator<StructureChain>";
          }
        }
        else
        {
          allocatorTemplates += "typename " + startUpperCase( stripPrefix( returnDataTypes[i], "VULKAN_HPP_NAMESPACE::" ) ) + "Allocator";
          if ( !definition )
          {
            allocatorTemplates += " = std::allocator<" + ( unique ? ( "UniqueHandle<" + returnDataTypes[i] + ", Dispatch>" ) : returnDataTypes[i] ) + ">";
          }
        }
        allocatorTemplates += ", ";
      }
    }
  }
  std::string uniqueHandleAllocatorTemplates;
  if ( unique && !allocatorTemplates.empty() )
  {
    uniqueHandleAllocatorTemplates = ", " + stripPostfix( allocatorTemplates, ", " );
    allocatorTemplates.clear();
  }
  return std::make_pair( allocatorTemplates, uniqueHandleAllocatorTemplates );
}

std::string VulkanHppGenerator::generateArgumentListEnhanced( std::vector<ParamData> const &            params,
                                                              std::vector<size_t> const &               returnParams,
                                                              std::map<size_t, VectorParamData> const & vectorParams,
                                                              std::set<size_t> const &                  skippedParams,
                                                              std::set<size_t> const &                  singularParams,
                                                              std::set<size_t> const &                  templatedParams,
                                                              bool                                      definition,
                                                              CommandFlavourFlags                       flavourFlags,
                                                              bool                                      withDispatcher ) const
{
  bool withAllocators = flavourFlags & CommandFlavourFlagBits::withAllocator;

  size_t defaultStartIndex = withAllocators ? ~0 : determineDefaultStartIndex( params, skippedParams );

  std::string argumentList;
  bool        encounteredArgument = false;
  for ( size_t i = 0; i < params.size(); ++i )
  {
    if ( skippedParams.find( i ) == skippedParams.end() )
    {
      if ( encounteredArgument )
      {
        argumentList += ", ";
      }
      bool hasDefaultAssignment = false;

      std::string composedType = params[i].type.compose( "VULKAN_HPP_NAMESPACE" );

      if ( singularParams.find( i ) != singularParams.end() )
      {
        assert( !params[i].optional );
        assert( params[i].type.isConstPointer() && !params[i].lenExpression.empty() && !isLenByStructMember( params[i].lenExpression, params ) &&
                params[i].type.type.starts_with( "Vk" ) );
        assert( !isHandleType( params[i].type.type ) );
        assert( composedType.ends_with( " *" ) );
        argumentList += stripPostfix( composedType, " *" ) + " & " + stripPluralS( startLowerCase( stripPrefix( params[i].name, "p" ) ) );
      }
      else if ( params[i].type.isConstPointer() )
      {
        assert( composedType.ends_with( " *" ) );
        std::string name = startLowerCase( stripPrefix( params[i].name, "p" ) );
        if ( params[i].lenExpression.empty() )
        {
          assert( withDispatcher || !isHandleType( params[i].type.type ) );
          assert( !params[i].type.prefix.empty() && ( params[i].type.postfix == "*" ) );
          assert( params[i].arraySizes.empty() );
          if ( params[i].type.type == "void" )
          {
            argumentList += ( templatedParams.find( i ) == templatedParams.end() ) ? ( composedType + " " + params[i].name )
                                                                                   : ( stripPrefix( params[i].name, "p" ) + "Type const & " + name );
          }
          else if ( params[i].optional )
          {
            argumentList += "Optional<" + stripPostfix( composedType, " *" ) + "> " + name +
                            ( ( definition || withAllocators ) ? "" : " VULKAN_HPP_DEFAULT_ARGUMENT_NULLPTR_ASSIGNMENT" );
            hasDefaultAssignment = true;
          }
          else
          {
            argumentList += stripPostfix( composedType, " *" ) + " & " + name;
          }
        }
        else
        {
          // a const-pointer with a non-empty len is either null-terminated (aka a string) or represented by an
          // ArrayProxy
          assert( params[i].arraySizes.empty() );
          if ( params[i].lenExpression == "null-terminated" )
          {
            assert( params[i].type.type == "char" );
            if ( params[i].optional )
            {
              argumentList +=
                "Optional<const std::string> " + name + ( ( definition || withAllocators ) ? "" : " VULKAN_HPP_DEFAULT_ARGUMENT_NULLPTR_ASSIGNMENT" );
              hasDefaultAssignment = true;
            }
            else
            {
              argumentList += "const std::string & " + name;
            }
          }
          else
          {
            // an ArrayProxy also covers no data, so any optional flag can be ignored here
            std::string type = stripPostfix( composedType, " *" );
            size_t      pos  = type.find( "void" );
            if ( pos != std::string::npos )
            {
              type.replace( pos, 4, stripPrefix( params[i].name, "p" ) + "Type" );
            }
            argumentList +=
              std::string( "VULKAN_HPP_NAMESPACE::" ) + ( params[i].strideParam.first.empty() ? "" : "Strided" ) + "ArrayProxy<" + type + "> const & " + name;
            if ( params[i].optional && !definition )
            {
              assert( params[i].strideParam.first.empty() );
              argumentList += " VULKAN_HPP_DEFAULT_ARGUMENT_NULLPTR_ASSIGNMENT";
              hasDefaultAssignment = true;
            }
          }
        }
      }
      else if ( params[i].type.isNonConstPointer() )
      {
        assert( withDispatcher || !isHandleType( params[i].type.type ) );
        assert( params[i].lenExpression.empty() && !params[i].optional );
        assert( composedType.ends_with( " *" ) );
        argumentList += stripPostfix( composedType, " *" ) + " & " + params[i].name;
      }
      else
      {
        assert( params[i].type.isValue() );
        argumentList += composedType + " " + params[i].name + generateCArraySizes( params[i].arraySizes );
      }
      argumentList += std::string( !definition && ( defaultStartIndex <= i ) && !hasDefaultAssignment ? " VULKAN_HPP_DEFAULT_ARGUMENT_ASSIGNMENT" : "" );
      encounteredArgument = true;
    }
  }
  if ( withAllocators )
  {
    if ( flavourFlags & CommandFlavourFlagBits::chained )
    {
      if ( encounteredArgument )
      {
        argumentList += ", ";
      }
      argumentList += "StructureChainAllocator & structureChainAllocator";
      encounteredArgument = true;
    }
    else
    {
      for ( auto sp : skippedParams )
      {
        if ( !params[sp].lenExpression.empty() )
        {
          if ( encounteredArgument )
          {
            argumentList += ", ";
          }
          std::string type;
          if ( templatedParams.find( sp ) != templatedParams.end() )
          {
            auto vectorParamIt = vectorParams.find( sp );
            if ( ( vectorParamIt != vectorParams.end() ) &&
                 ( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->first ) != returnParams.end() ) &&
                 ( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->second.lenParam ) != returnParams.end() ) )
            {
              type = "Uint8_t";
            }
            else
            {
              type = stripPrefix( params[sp].name, "p" ) + "Type";
            }
          }
          else
          {
            type = ( params[sp].type.type == "void" ) ? "Uint8_t" : startUpperCase( stripPrefix( params[sp].type.type, "Vk" ) );
          }
          argumentList += type + "Allocator & " + startLowerCase( type ) + "Allocator";
          encounteredArgument = true;
        }
      }
    }
  }
  if ( withDispatcher )
  {
    if ( encounteredArgument )
    {
      argumentList += ", ";
    }
    argumentList += std::string( "Dispatch const & d" ) + ( definition ? "" : " VULKAN_HPP_DEFAULT_DISPATCHER_ASSIGNMENT" );
  }
  return argumentList;
}

std::string VulkanHppGenerator::generateArgumentListStandard( std::vector<ParamData> const & params, std::set<size_t> const & skippedParams ) const
{
  std::string argumentList;
  for ( size_t i = 0; i < params.size(); ++i )
  {
    if ( skippedParams.find( i ) == skippedParams.end() )
    {
      argumentList += params[i].type.compose( "VULKAN_HPP_NAMESPACE" ) + " " + params[i].name + generateCArraySizes( params[i].arraySizes ) + ", ";
    }
  }
  argumentList += "Dispatch const & d ";
  return argumentList;
}

std::string VulkanHppGenerator::generateArgumentTemplates( std::vector<ParamData> const &            params,
                                                           std::vector<size_t> const &               returnParams,
                                                           std::map<size_t, VectorParamData> const & vectorParams,
                                                           std::set<size_t> const &                  templatedParams,
                                                           CommandFlavourFlags                       flavourFlags,
                                                           bool                                      raii ) const
{
  std::string argumentTemplates;
  if ( !templatedParams.empty() )
  {
    assert( !( flavourFlags & CommandFlavourFlagBits::chained ) );
    for ( auto t : templatedParams )
    {
      assert( params[t].name.starts_with( "p" ) );
      auto vectorParamIt = vectorParams.find( t );
      if ( ( vectorParamIt == vectorParams.end() ) || ( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->first ) == returnParams.end() ) ||
           ( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->second.lenParam ) == returnParams.end() ) )
      {
        // only templated parameters that are not part of an enumeration are really templated
        argumentTemplates += "typename " + stripPrefix( params[t].name, "p" ) + "Type, ";
      }
    }
  }
  else if ( flavourFlags & CommandFlavourFlagBits::chained )
  {
    argumentTemplates = ( returnParams.size() == 1 ) ? "typename X, typename Y, typename... Z, " : "typename StructureChain, ";
  }
  if ( !argumentTemplates.empty() && raii )
  {
    argumentTemplates = "template <" + stripPostfix( argumentTemplates, ", " ) + ">";
  }
  return argumentTemplates;
}

std::string VulkanHppGenerator::generateBaseTypes() const
{
  assert( !m_baseTypes.empty() );
  const std::string basetypesTemplate = R"(
  //==================
  //=== BASE TYPEs ===
  //==================

${basetypes}
)";

  std::string basetypes;
  for ( auto const & baseType : m_baseTypes )
  {
    // filter out VkFlags and VkFlags64, as they are mapped to our own Flags class, and basetypes without any type information
    if ( ( baseType.first != "VkFlags" ) && ( baseType.first != "VkFlags64" ) && !baseType.second.typeInfo.type.empty() )
    {
      basetypes += "  using " + stripPrefix( baseType.first, "Vk" ) + " = " + baseType.second.typeInfo.compose( "VULKAN_HPP_NAMESPACE" ) + ";\n";
    }
  }

  return replaceWithMap( basetypesTemplate, { { "basetypes", basetypes } } );
}

std::string VulkanHppGenerator::generateBitmask( std::map<std::string, BitmaskData>::const_iterator bitmaskIt, std::string const & surroundingProtect ) const
{
  auto bitmaskBitsIt = m_enums.find( bitmaskIt->second.require );
  assert( bitmaskBitsIt != m_enums.end() );

  std::string bitmaskName    = stripPrefix( bitmaskIt->first, "Vk" );
  std::string enumName       = stripPrefix( bitmaskBitsIt->first, "Vk" );
  auto        aliasBitmaskIt = findAlias( bitmaskIt->first, m_bitmaskAliases );
  std::string alias =
    ( aliasBitmaskIt == m_bitmaskAliases.end() ) ? "" : ( "  using " + stripPrefix( aliasBitmaskIt->first, "Vk" ) + " = " + bitmaskName + ";\n" );

  std::string allFlags;
  if ( bitmaskBitsIt->second.values.empty() )
  {
    allFlags = " {};";
  }
  else
  {
    bool        encounteredFlag = false;
    std::string previousEnter, previousLeave;
    for ( auto const & value : bitmaskBitsIt->second.values )
    {
      // only list non-alias values in the allFlags trait
      if ( value.alias.empty() )
      {
        // if the value's protect differs from the surrounding protect, generate protection code
        std::string enter, leave;
        if ( !value.protect.empty() && ( value.protect != surroundingProtect ) )
        {
          tie( enter, leave ) = generateProtection( value.protect );
        }
        std::string valueName = generateEnumValueName( bitmaskBitsIt->first, value.name, true );
        allFlags += ( ( previousEnter != enter ) ? ( "\n" + previousLeave + enter ) : "\n" ) + "        " + ( encounteredFlag ? "| " : "  " ) + enumName +
                    "::" + valueName;
        encounteredFlag = true;
        previousEnter   = enter;
        previousLeave   = leave;
      }
    }
    if ( !previousLeave.empty() )
    {
      allFlags += "\n" + previousLeave;
    }
    allFlags += ";";
  }

  static const std::string bitmaskTemplate = R"(
  using ${bitmaskName} = Flags<${enumName}>;
${alias}

  template <> struct FlagTraits<${enumName}>
  {
    static VULKAN_HPP_CONST_OR_CONSTEXPR bool isBitmask = true;
    static VULKAN_HPP_CONST_OR_CONSTEXPR ${bitmaskName} allFlags = ${allFlags}
  };
)";

  return replaceWithMap( bitmaskTemplate, { { "alias", alias }, { "allFlags", allFlags }, { "bitmaskName", bitmaskName }, { "enumName", enumName } } );
}

std::string VulkanHppGenerator::generateBitmasksToString() const
{
  const std::string bitmasksToStringTemplate = R"(
  //==========================
  //=== BITMASKs to_string ===
  //==========================

${bitmasksToString}
)";

  std::string           bitmasksToString;
  std::set<std::string> listedBitmasks;
  for ( auto const & feature : m_features )
  {
    bitmasksToString += generateBitmasksToString( feature.requireData, listedBitmasks, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    bitmasksToString += generateBitmasksToString( extension.requireData, listedBitmasks, extension.name );
  }

  return replaceWithMap( bitmasksToStringTemplate, { { "bitmasksToString", bitmasksToString } } );
}

std::string VulkanHppGenerator::generateBitmasksToString( std::vector<RequireData> const & requireData,
                                                          std::set<std::string> &          listedBitmasks,
                                                          std::string const &              title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto bitmaskIt = m_bitmasks.find( type );
      if ( ( bitmaskIt != m_bitmasks.end() ) && ( listedBitmasks.find( type ) == listedBitmasks.end() ) )
      {
        listedBitmasks.insert( type );
        str += generateBitmaskToString( bitmaskIt );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateBitmaskToString( std::map<std::string, BitmaskData>::const_iterator bitmaskIt ) const
{
  auto bitmaskBitsIt = m_enums.find( bitmaskIt->second.require );
  assert( bitmaskBitsIt != m_enums.end() );

  std::string bitmaskName = stripPrefix( bitmaskIt->first, "Vk" );
  std::string enumName    = stripPrefix( bitmaskBitsIt->first, "Vk" );

  std::string str;
  if ( bitmaskBitsIt->second.values.empty() )
  {
    static std::string bitmaskToStringTemplate = R"(
  VULKAN_HPP_INLINE std::string to_string( ${bitmaskName} )
  {
    return "{}";
  }
)";
    str += replaceWithMap( bitmaskToStringTemplate, { { "bitmaskName", bitmaskName } } );
  }
  else
  {
    static const std::string bitmaskToStringTemplate = R"(
  VULKAN_HPP_INLINE std::string to_string( ${bitmaskName} value )
  {
    if ( !value )
      return "{}";

    std::string result;
${toStringChecks}
    return "{ " + result.substr( 0, result.size() - 3 ) + " }";
  }
)";

    std::string toStringChecks;
    std::string previousEnter, previousLeave;
    for ( auto const & value : bitmaskBitsIt->second.values )
    {
      auto [enter, leave]   = generateProtection( value.protect );
      std::string valueName = generateEnumValueName( bitmaskBitsIt->first, value.name, true );
      if ( value.singleBit )
      {
        toStringChecks += ( ( previousEnter != enter ) ? ( previousLeave + enter ) : "" ) + "    if ( value & " + enumName + "::" + valueName +
                          " ) result += \"" + valueName.substr( 1 ) + " | \";\n";
      }
      previousEnter = enter;
      previousLeave = leave;
    }
    if ( !previousLeave.empty() )
    {
      assert( previousLeave.ends_with( "\n" ) );
      toStringChecks += previousLeave;
      previousLeave.resize( previousLeave.size() - strlen( "\n" ) );
    }

    str += replaceWithMap( bitmaskToStringTemplate, { { "bitmaskName", bitmaskName }, { "toStringChecks", toStringChecks } } );
  }

  return str;
}

std::string VulkanHppGenerator::generateCallArgumentsEnhanced( CommandData const &      commandData,
                                                               size_t                   initialSkipCount,
                                                               bool                     nonConstPointerAsNullptr,
                                                               std::set<size_t> const & singularParams,
                                                               std::set<size_t> const & templatedParams,
                                                               bool                     raiiHandleMemberFunction ) const
{
  assert( initialSkipCount <= commandData.params.size() );
  std::string arguments;
  bool        encounteredArgument = false;
  if ( raiiHandleMemberFunction )
  {
    switch ( initialSkipCount )
    {
      case 1:
        assert( isHandleType( commandData.params[0].type.type ) && commandData.params[0].type.isValue() );
        assert( commandData.params[0].arraySizes.empty() && commandData.params[0].lenExpression.empty() );
        assert( commandData.params[0].type.type == commandData.handle );
        arguments           = "static_cast<" + commandData.handle + ">( m_" + startLowerCase( stripPrefix( commandData.handle, "Vk" ) ) + " )";
        encounteredArgument = true;
        break;
      case 2:
        {
          assert( isHandleType( commandData.params[0].type.type ) && commandData.params[0].type.isValue() );
          assert( commandData.params[0].arraySizes.empty() && commandData.params[0].lenExpression.empty() );
          assert( commandData.params[0].type.type == commandData.handle );
          auto handleIt = m_handles.find( commandData.params[1].type.type );
          assert( handleIt != m_handles.end() );
          arguments = "static_cast<" + commandData.handle + ">( m_" + startLowerCase( stripPrefix( commandData.handle, "Vk" ) ) + " )";

          assert( commandData.params[1].type.isValue() && commandData.params[1].arraySizes.empty() && commandData.params[1].lenExpression.empty() );
          arguments += ", static_cast<" + commandData.params[1].type.type + ">( m_" +
                       generateRAIIHandleConstructorParamName( handleIt->first, handleIt->second.destructorIt ) + " )";
          encounteredArgument = true;
        }
        break;
    }
  }
  else
  {
    for ( size_t i = 0; i < initialSkipCount; ++i )
    {
      if ( encounteredArgument )
      {
        arguments += ", ";
      }
      assert( isHandleType( commandData.params[i].type.type ) && commandData.params[i].type.isValue() );
      assert( commandData.params[i].arraySizes.empty() && commandData.params[i].lenExpression.empty() );
      arguments += "m_" + startLowerCase( stripPrefix( commandData.params[i].type.type, "Vk" ) );
      encounteredArgument = true;
    }
  }
  for ( size_t i = initialSkipCount; i < commandData.params.size(); ++i )
  {
    if ( encounteredArgument )
    {
      arguments += ", ";
    }
    arguments += generateCallArgumentEnhanced( commandData.params, i, nonConstPointerAsNullptr, singularParams, templatedParams );
    encounteredArgument = true;
  }
  return arguments;
}

std::string VulkanHppGenerator::generateCallArgumentsRAIIFactory( std::vector<ParamData> const & params,
                                                                  size_t                         initialSkipCount,
                                                                  std::set<size_t> const &       skippedParams,
                                                                  std::set<size_t> const &       singularParams ) const
{
  assert( initialSkipCount <= params.size() );
  std::string arguments = "*this";
  // skip the last parameter!
  for ( size_t i = initialSkipCount; i < params.size() - 1; ++i )
  {
    if ( skippedParams.find( i ) == skippedParams.end() )
    {
      std::string argument = params[i].name;
      if ( !params[i].type.isValue() )
      {
        argument = startLowerCase( stripPrefix( argument, "p" ) );
        if ( singularParams.find( i ) != singularParams.end() )
        {
          argument = stripPluralS( argument );
        }
      }
      else
      {
        assert( singularParams.find( i ) == singularParams.end() );
      }
      arguments += ", " + argument;
    }
  }
  return arguments;
}

std::string VulkanHppGenerator::generateCallArgumentsStandard( std::string const & handle, std::vector<ParamData> const & params ) const
{
  std::string arguments;
  bool        encounteredArgument = false;
  for ( auto const & param : params )
  {
    if ( encounteredArgument )
    {
      arguments += ", ";
    }
    if ( ( param.type.type == handle ) && param.type.isValue() )
    {
      assert( param.arraySizes.empty() && param.lenExpression.empty() );
      arguments += "m_" + startLowerCase( stripPrefix( param.type.type, "Vk" ) );
    }
    else
    {
      std::string argument = param.name;
      if ( param.type.type.starts_with( "Vk" ) )
      {
        if ( !param.arraySizes.empty() )
        {
          assert( param.arraySizes.size() == 1 );
          assert( param.type.isValue() );
          assert( param.type.postfix.empty() );
          argument = "reinterpret_cast<" + param.type.compose( "" ) + " *>( " + argument + " )";
        }
        else if ( param.type.isValue() )
        {
          argument = "static_cast<" + param.type.type + ">( " + argument + " )";
        }
        else
        {
          assert( !param.type.postfix.empty() );
          argument = "reinterpret_cast<" + param.type.compose( "" ) + ">( " + argument + " )";
        }
      }
      arguments += argument;
    }
    encounteredArgument = true;
  }
  return arguments;
}

std::string VulkanHppGenerator::generateCallArgumentEnhanced( std::vector<ParamData> const & params,
                                                              size_t                         paramIndex,
                                                              bool                           nonConstPointerAsNullptr,
                                                              std::set<size_t> const &       singularParams,
                                                              std::set<size_t> const &       templatedParams ) const
{
  std::string       argument;
  ParamData const & param = params[paramIndex];
  if ( param.type.isConstPointer() || ( specialPointerTypes.find( param.type.type ) != specialPointerTypes.end() ) )
  {
    // parameter is a const-pointer or one of the special pointer types that are considered to be const-pointers
    argument = generateCallArgumentEnhancedConstPointer( param, paramIndex, singularParams, templatedParams );
  }
  else if ( param.type.isNonConstPointer() && ( specialPointerTypes.find( param.type.type ) == specialPointerTypes.end() ) )
  {
    // parameter is a non-const pointer and none of the special pointer types, that are considered const-pointers
    argument = generateCallArgumentEnhancedNonConstPointer( param, paramIndex, nonConstPointerAsNullptr, singularParams );
  }
  else
  {
    argument = generateCallArgumentEnhancedValue( params, paramIndex, singularParams );
  }
  assert( !argument.empty() );
  return argument;
}

std::string VulkanHppGenerator::generateCallArgumentEnhancedConstPointer( ParamData const &        param,
                                                                          size_t                   paramIndex,
                                                                          std::set<size_t> const & singularParams,
                                                                          std::set<size_t> const & templatedParams ) const
{
  std::string argument;
  std::string name = startLowerCase( stripPrefix( param.name, "p" ) );
  if ( isHandleType( param.type.type ) && param.type.isValue() )
  {
    assert( !param.optional );
    // if at all, this is the first argument, and it's the implicitly provided member handle
    assert( paramIndex == 0 );
    assert( param.arraySizes.empty() && param.lenExpression.empty() );
    argument = "m_" + startLowerCase( stripPrefix( param.type.type, "Vk" ) );
  }
  else if ( param.lenExpression.empty() )
  {
    // this const-pointer parameter has no length, that is it's a const-pointer to a single value
    if ( param.type.type == "void" )
    {
      argument = ( templatedParams.find( paramIndex ) == templatedParams.end() )
                 ? param.name
                 : "reinterpret_cast<" + param.type.compose( "VULKAN_HPP_NAMESPACE" ) + ">( &" + name + " )";
    }
    else if ( param.optional )
    {
      argument = "static_cast<" + param.type.compose( "VULKAN_HPP_NAMESPACE" ) + ">( " + name + " )";
    }
    else
    {
      argument = "&" + name;
    }
    if ( param.type.type.starts_with( "Vk" ) )
    {
      argument = "reinterpret_cast<" + param.type.compose( "" ) + ">( " + argument + " )";
    }
  }
  else if ( param.lenExpression == "null-terminated" )
  {
    // this const-pointer parameter is "null-terminated", that is it's a string
    assert( ( param.type.type == "char" ) && param.arraySizes.empty() );
    if ( param.optional )
    {
      argument = name + " ? " + name + "->c_str() : nullptr";
    }
    else
    {
      argument = name + ".c_str()";
    }
  }
  else
  {
    // this const-pointer parameter has some explicit length
    if ( singularParams.find( paramIndex ) != singularParams.end() )
    {
      assert( !param.optional );
      argument = "&" + stripPluralS( name );
    }
    else
    {
      // this const-parameter is represented by some array, where data() also works with no data (optional)
      argument = name + ".data()";
    }
    if ( param.type.type.starts_with( "Vk" ) || ( param.type.type == "void" ) )
    {
      argument = "reinterpret_cast<" + param.type.compose( "" ) + ">( " + argument + " )";
    }
  }
  return argument;
}

std::string VulkanHppGenerator::generateCallArgumentEnhancedNonConstPointer( ParamData const &        param,
                                                                             size_t                   paramIndex,
                                                                             bool                     nonConstPointerAsNullptr,
                                                                             std::set<size_t> const & singularParams ) const
{
  std::string argument;
  std::string name = startLowerCase( stripPrefix( param.name, "p" ) );
  if ( param.lenExpression.empty() )
  {
    assert( param.arraySizes.empty() );
    if ( param.type.type.starts_with( "Vk" ) )
    {
      argument = "reinterpret_cast<" + param.type.compose( "" ) + ">( &" + name + " )";
    }
    else
    {
      assert( !param.optional );
      argument = "&" + name;
    }
  }
  else
  {
    // the non-const pointer has a lenExpression -> it will be represented by some array
    assert( param.arraySizes.empty() );
    if ( nonConstPointerAsNullptr )
    {
      argument = "nullptr";
    }
    else
    {
      if ( singularParams.find( paramIndex ) != singularParams.end() )
      {
        argument = "&" + stripPluralS( name );
      }
      else
      {
        // get the data of the array, which also covers no data -> no need to look at param.optional
        argument = name + ".data()";
      }
      if ( param.type.type.starts_with( "Vk" ) || ( param.type.type == "void" ) )
      {
        argument = "reinterpret_cast<" + param.type.compose( "" ) + ">( " + argument + " )";
      }
    }
  }
  return argument;
}

std::string VulkanHppGenerator::generateCallArgumentEnhancedValue( std::vector<ParamData> const & params,
                                                                   size_t                         paramIndex,
                                                                   std::set<size_t> const &       singularParams ) const
{
  std::string       argument;
  ParamData const & param = params[paramIndex];
  assert( param.lenExpression.empty() );
  if ( param.type.type.starts_with( "Vk" ) )
  {
    if ( param.arraySizes.empty() )
    {
      // check if this param is used as the len of an other param
      auto pointerIt = std::find_if( params.begin(), params.end(), [&param]( ParamData const & pd ) { return pd.lenExpression == param.name; } );
      if ( pointerIt != params.end() )
      {
        assert( !param.optional );
        argument = startLowerCase( stripPrefix( pointerIt->name, "p" ) ) + ".size()";
        if ( pointerIt->type.type == "void" )
        {
          argument += " * sizeof( " + stripPrefix( pointerIt->name, "p" ) + "Type )";
        }
      }
      else
      {
        argument = "static_cast<" + param.type.compose( "" ) + ">( " + param.name + " )";
      }
      // check if this param is used as the stride of an other param
      assert( std::find_if( params.begin(), params.end(), [paramIndex]( ParamData const & pd ) { return pd.strideParam.second == paramIndex; } ) ==
              params.end() );
    }
    else
    {
      assert( !param.optional );
      assert( param.arraySizes.size() == 1 );
      assert( param.type.prefix == "const" );
      argument = "reinterpret_cast<" + param.type.compose( "" ) + " *>( " + param.name + " )";
    }
  }
  else
  {
    auto pointerIt = std::find_if( params.begin(), params.end(), [&param]( ParamData const & pd ) { return pd.lenExpression == param.name; } );
    if ( pointerIt != params.end() )
    {
      // this parameter is the len of some other -> replace it with that parameter's size
      assert( param.arraySizes.empty() );
      assert( ( param.type.type == "size_t" ) || ( param.type.type == "uint32_t" ) );
      if ( singularParams.find( paramIndex ) == singularParams.end() )
      {
        argument = startLowerCase( stripPrefix( pointerIt->name, "p" ) ) + ".size()";
        if ( pointerIt->type.type == "void" )
        {
          argument += " * sizeof( " + stripPrefix( pointerIt->name, "p" ) + "Type )";
        }
      }
      else
      {
        if ( pointerIt->type.type == "void" )
        {
          argument = "sizeof( " + stripPrefix( pointerIt->name, "p" ) + "Type )";
        }
        else
        {
          argument = "1";
        }
      }
    }
    else
    {
      assert( !param.optional );
      assert( param.arraySizes.size() <= 1 );
      pointerIt = std::find_if( params.begin(), params.end(), [paramIndex]( ParamData const & pd ) { return pd.strideParam.second == paramIndex; } );
      if ( pointerIt != params.end() )
      {
        // this parameter is the stride of some other -> replace it with that parameter's stride
        assert( param.arraySizes.empty() );
        assert( param.type.type == "uint32_t" );
        assert( pointerIt->strideParam.first == param.name );
        argument = startLowerCase( stripPrefix( pointerIt->name, "p" ) ) + ".stride()";
      }
      else
      {
        argument = param.name;
      }
    }
  }
  return argument;
}

std::string VulkanHppGenerator::generateCallSequence( std::string const &                       name,
                                                      CommandData const &                       commandData,
                                                      std::vector<size_t> const &               returnParams,
                                                      std::map<size_t, VectorParamData> const & vectorParams,
                                                      size_t                                    initialSkipCount,
                                                      std::set<size_t> const &                  singularParams,
                                                      std::set<size_t> const &                  templatedParams,
                                                      CommandFlavourFlags                       flavourFlags,
                                                      bool                                      raii ) const
{
  std::string dispatcher = raii ? "getDispatcher()->" : "d.";
  // if at least one returnParam is a size value of a vector param (and no singular params), we need two calls
  if ( singularParams.empty() &&
       ( std::find_if( returnParams.begin(),
                       returnParams.end(),
                       [&vectorParams]( size_t rp )
                       {
                         return ( std::find_if( vectorParams.begin(), vectorParams.end(), [rp]( auto const & vp ) { return vp.second.lenParam == rp; } ) !=
                                  vectorParams.end() );
                       } ) != returnParams.end() ) )
  {
    auto vectorParamIt = vectorParams.begin();
    assert( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->first ) != returnParams.end() );
    assert( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->second.lenParam ) != returnParams.end() );

    std::string firstCallArguments  = generateCallArgumentsEnhanced( commandData, initialSkipCount, true, {}, templatedParams, raii );
    std::string secondCallArguments = generateCallArgumentsEnhanced( commandData, initialSkipCount, false, {}, templatedParams, raii );
    std::string vectorName          = startLowerCase( stripPrefix( commandData.params[vectorParamIt->first].name, "p" ) );
    std::string vectorSize          = startLowerCase( stripPrefix( commandData.params[vectorParamIt->second.lenParam].name, "p" ) );

    if ( flavourFlags & CommandFlavourFlagBits::chained )
    {
      assert( vectorParams.size() == 1 );
      // chained data needs some more handling!!
      std::string vectorElementType = stripPostfix( commandData.params[vectorParamIt->first].type.compose( "VULKAN_HPP_NAMESPACE" ), " *" );

      if ( commandData.returnType == "VkResult" )
      {
        const std::string callSequenceTemplate = R"(VkResult result;
    do
    {
      result = ${dispatcher}${vkCommand}( ${firstCallArguments} );
      if ( ( result == VK_SUCCESS ) && ${counterName} )
      {
        structureChains.resize( ${counterName} );
        ${vectorName}.resize( ${counterName} );
        for ( ${counterType} i = 0; i < ${counterName}; i++ )
        {
          ${vectorName}[i].pNext = structureChains[i].template get<${vectorElementType}>().pNext;
        }
        result = ${dispatcher}${vkCommand}( ${secondCallArguments} );
      }
    } while ( result == VK_INCOMPLETE );)";

        return replaceWithMap( callSequenceTemplate,
                               { { "counterName", startLowerCase( stripPrefix( commandData.params[vectorParamIt->second.lenParam].name, "p" ) ) },
                                 { "counterType", commandData.params[vectorParamIt->second.lenParam].type.type },
                                 { "dispatcher", dispatcher },
                                 { "firstCallArguments", firstCallArguments },
                                 { "secondCallArguments", secondCallArguments },
                                 { "vectorElementType", vectorElementType },
                                 { "vectorName", vectorName },
                                 { "vkCommand", name } } );
      }
      else
      {
        const std::string callSequenceTemplate =
          R"(${dispatcher}${vkCommand}( ${firstCallArguments} );
    structureChains.resize( ${counterName} );
    ${vectorName}.resize( ${counterName} );
    for ( ${counterType} i = 0; i < ${counterName}; i++ )
    {
      ${vectorName}[i].pNext = structureChains[i].template get<${vectorElementType}>().pNext;
    }
    ${dispatcher}${vkCommand}( ${secondCallArguments} );)";

        return replaceWithMap( callSequenceTemplate,
                               { { "counterName", startLowerCase( stripPrefix( commandData.params[vectorParamIt->second.lenParam].name, "p" ) ) },
                                 { "counterType", commandData.params[vectorParamIt->second.lenParam].type.type },
                                 { "dispatcher", dispatcher },
                                 { "firstCallArguments", firstCallArguments },
                                 { "secondCallArguments", secondCallArguments },
                                 { "vectorElementType", vectorElementType },
                                 { "vectorName", vectorName },
                                 { "vkCommand", name } } );
      }
    }
    else if ( commandData.returnType == "VkResult" )
    {
      std::string resizes;
      for ( auto const & vp : vectorParams )
      {
        assert( ( std::find( returnParams.begin(), returnParams.end(), vp.first ) != returnParams.end() ) &&
                ( std::find( returnParams.begin(), returnParams.end(), vp.second.lenParam ) != returnParams.end() ) );
        resizes += startLowerCase( stripPrefix( commandData.params[vp.first].name, "p" ) ) + ".resize( " +
                   startLowerCase( stripPrefix( commandData.params[vp.second.lenParam].name, "p" ) ) + " );\n";
      }
      resizes.pop_back();

      switch ( commandData.successCodes.size() )
      {
        case 1:
          {
            assert( commandData.successCodes[0] == "VK_SUCCESS" );
            std::string const callSequenceTemplate = R"(${dispatcher}${vkCommand}( ${firstCallArguments} );
      ${resizes}
      ${dispatcher}${vkCommand}( ${secondCallArguments} );
)";

            return replaceWithMap( callSequenceTemplate,
                                   { { "dispatcher", dispatcher },
                                     { "firstCallArguments", firstCallArguments },
                                     { "secondCallArguments", secondCallArguments },
                                     { "resizes", resizes },
                                     { "vkCommand", name } } );
          }
        case 2:
          {
            assert( ( commandData.successCodes[0] == "VK_SUCCESS" ) && ( commandData.successCodes[1] == "VK_INCOMPLETE" ) );
            std::string const callSequenceTemplate = R"(VkResult result;
    do
    {
      result = ${dispatcher}${vkCommand}( ${firstCallArguments} );
      if ( ( result == VK_SUCCESS ) && ${counterName} )
      {
        ${resizes}
        result = ${dispatcher}${vkCommand}( ${secondCallArguments} );
      }
    } while ( result == VK_INCOMPLETE );)";

            return replaceWithMap( callSequenceTemplate,
                                   { { "counterName", startLowerCase( stripPrefix( commandData.params[vectorParamIt->second.lenParam].name, "p" ) ) },
                                     { "dispatcher", dispatcher },
                                     { "firstCallArguments", firstCallArguments },
                                     { "secondCallArguments", secondCallArguments },
                                     { "resizes", resizes },
                                     { "vkCommand", name } } );
          }
        default: assert( false ); return "";
      }
    }
    else
    {
      // no need to enumerate here, just two calls
      assert( commandData.returnType == "void" );
      std::string const callSequenceTemplate = R"(${dispatcher}${vkCommand}( ${firstCallArguments} );
    ${vectorName}.resize( ${vectorSize} );
    ${dispatcher}${vkCommand}( ${secondCallArguments} );)";

      return replaceWithMap( callSequenceTemplate,
                             { { "dispatcher", dispatcher },
                               { "firstCallArguments", firstCallArguments },
                               { "secondCallArguments", secondCallArguments },
                               { "vectorName", vectorName },
                               { "vectorSize", vectorSize },
                               { "vkCommand", name } } );
    }
  }
  else
  {
    std::string const callSequenceTemplate = R"(${resultAssignment}${dispatcher}${vkCommand}( ${callArguments} );)";

    std::string callArguments    = generateCallArgumentsEnhanced( commandData, initialSkipCount, false, singularParams, templatedParams, raii );
    std::string resultAssignment = generateResultAssignment( commandData );

    return replaceWithMap(
      callSequenceTemplate,
      { { "callArguments", callArguments }, { "dispatcher", dispatcher }, { "resultAssignment", resultAssignment }, { "vkCommand", name } } );
  }
}

std::string
  VulkanHppGenerator::generateCommand( std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::string cmd;
  if ( commandData.returnType == "VkResult" )
  {
    cmd = generateCommandResult( name, commandData, initialSkipCount, definition, raii );
  }
  else if ( commandData.returnType == "void" )
  {
    cmd = generateCommandVoid( name, commandData, initialSkipCount, definition, raii );
  }
  else
  {
    cmd = generateCommandValue( name, commandData, initialSkipCount, definition, raii );
  }

  if ( cmd.empty() )
  {
    throw std::runtime_error( "Never encountered a function like <" + name + "> !" );
  }

  return cmd;
}

std::string VulkanHppGenerator::generateCommandDefinitions() const
{
  const std::string commandDefinitionsTemplate = R"(
  //===========================
  //=== COMMAND Definitions ===
  //===========================

${commandDefinitions}
)";

  std::string           commandDefinitions;
  std::set<std::string> listedCommands;  // some commands are listed with more than one extension!
  for ( auto const & feature : m_features )
  {
    commandDefinitions += generateCommandDefinitions( feature.requireData, listedCommands, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    commandDefinitions += generateCommandDefinitions( extension.requireData, listedCommands, extension.name );
  }

  return replaceWithMap( commandDefinitionsTemplate, { { "commandDefinitions", commandDefinitions } } );
}

std::string VulkanHppGenerator::generateCommandDefinitions( std::vector<RequireData> const & requireData,
                                                            std::set<std::string> &          listedCommands,
                                                            std::string const &              title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & command : require.commands )
    {
      if ( listedCommands.insert( command ).second )
      {
        str += generateCommandDefinitions( command, getCommandData( command ).handle );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateCommandDefinitions( std::string const & command, std::string const & handle ) const
{
  auto const & commandData = getCommandData( command );

  std::string str = "\n" + generateCommand( command, commandData, handle.empty() ? 0 : 1, true, false );

  // special handling for destroy functions, filter out alias functions
  std::string commandName = generateCommandName( command, commandData.params, 1 );
  if ( ( m_commandAliases.find( command ) == m_commandAliases.end() ) &&
       ( ( ( command.substr( 2, 7 ) == "Destroy" ) && ( commandName != "destroy" ) ) || ( command.substr( 2, 4 ) == "Free" ) ||
         ( command == "vkReleasePerformanceConfigurationINTEL" ) ) )
  {
    CommandData specialCommandData = commandData;
    assert( ( 1 < specialCommandData.params.size() ) && ( specialCommandData.params[0].type.type == handle ) );
    specialCommandData.params[1].optional = false;  // make sure, the object to destroy/free/release is not optional in the shortened version!

    std::string destroyCommandString = generateCommand( command, specialCommandData, handle.empty() ? 0 : 1, true, false );
    std::string shortenedName;
    if ( command.substr( 2, 7 ) == "Destroy" )
    {
      shortenedName = "destroy";
    }
    else if ( command.substr( 2, 4 ) == "Free" )
    {
      shortenedName = "free";
    }
    else
    {
      assert( command == "vkReleasePerformanceConfigurationINTEL" );
      shortenedName = "release";
    }
    size_t pos = destroyCommandString.find( commandName );
    while ( pos != std::string::npos )
    {
      destroyCommandString.replace( pos, commandName.length(), shortenedName );
      pos = destroyCommandString.find( commandName, pos );
    }

    // special handling for "free", to prevent interfering with MSVC debug free!
    if ( shortenedName == "free" )
    {
      std::string toEncloseString = stripPrefix( handle, "Vk" ) + "::free";
      std::string enclosedString  = "( " + toEncloseString + " )";
      pos                         = destroyCommandString.find( toEncloseString );
      while ( pos != std::string::npos )
      {
        destroyCommandString.replace( pos, toEncloseString.length(), enclosedString );
        pos = destroyCommandString.find( toEncloseString, pos + enclosedString.length() );
      }
    }

    // we need to remove the default argument for the first argument, to prevent ambiguities!
    assert( 1 < commandData.params.size() );
    pos = destroyCommandString.find( commandData.params[1].name );  // skip the standard version of the function
    assert( pos != std::string::npos );
    pos = destroyCommandString.find( commandData.params[1].name,
                                     pos + 1 );  // get the argument to destroy in the advanced version
    assert( pos != std::string::npos );
    pos = destroyCommandString.find( " VULKAN_HPP_DEFAULT_ARGUMENT_ASSIGNMENT", pos );
    if ( pos != std::string::npos )
    {
      destroyCommandString.erase( pos, strlen( " VULKAN_HPP_DEFAULT_ARGUMENT_ASSIGNMENT" ) );
    }
    str += "\n" + destroyCommandString;
  }
  return str;
}

std::string VulkanHppGenerator::generateCommandEnhanced( std::string const &                       name,
                                                         CommandData const &                       commandData,
                                                         size_t                                    initialSkipCount,
                                                         bool                                      definition,
                                                         std::map<size_t, VectorParamData> const & vectorParams,
                                                         std::vector<size_t> const &               returnParams,
                                                         CommandFlavourFlags                       flavourFlags ) const
{
  bool singular = flavourFlags & CommandFlavourFlagBits::singular;

  assert( vectorParams.empty() || ( vectorParams.begin()->second.lenParam != INVALID_INDEX ) );
  assert( !singular || !returnParams.empty() );  // if singular is true, then there is at least one returnParam !

  std::set<size_t> skippedParams = determineSkippedParams( commandData.params, initialSkipCount, vectorParams, returnParams, singular );
  // special handling for vkGetMemoryHostPointerPropertiesEXT: here, we really need to stick with the const void * parameter !
  std::set<size_t> templatedParams = ( name == "vkGetMemoryHostPointerPropertiesEXT" ) ? std::set<size_t>() : determineVoidPointerParams( commandData.params );
  std::set<size_t> singularParams  = singular ? determineSingularParams( returnParams[0], vectorParams ) : std::set<size_t>();
  std::pair<bool, std::map<size_t, std::vector<size_t>>> vectorSizeCheck =
    needsVectorSizeCheck( commandData.params, vectorParams, returnParams, singularParams, skippedParams );
  bool enumerating = determineEnumeration( vectorParams, returnParams );

  std::vector<std::string> dataTypes = determineDataTypes( commandData.params, vectorParams, returnParams, templatedParams );
  std::string              dataType  = combineDataTypes( vectorParams, returnParams, enumerating, dataTypes, flavourFlags, false );

  std::string argumentTemplates = generateArgumentTemplates( commandData.params, returnParams, vectorParams, templatedParams, flavourFlags, false );
  auto [allocatorTemplates, uniqueHandleAllocatorTemplates] = generateAllocatorTemplates( returnParams, dataTypes, vectorParams, flavourFlags, definition );
  std::string typenameCheck                                 = generateTypenameCheck( returnParams, vectorParams, definition, dataTypes, flavourFlags );
  std::string nodiscard      = generateNoDiscard( !returnParams.empty(), 1 < commandData.successCodes.size(), 1 < commandData.errorCodes.size() );
  std::string returnType     = generateReturnType( commandData, returnParams, vectorParams, flavourFlags, false, dataType );
  std::string className      = initialSkipCount ? stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" ) : "";
  std::string classSeparator = commandData.handle.empty() ? "" : "::";
  std::string commandName    = generateCommandName( name, commandData.params, initialSkipCount, flavourFlags );
  std::string argumentList   = generateArgumentListEnhanced(
    commandData.params, returnParams, vectorParams, skippedParams, singularParams, templatedParams, definition, flavourFlags, true );
  std::string constString    = commandData.handle.empty() ? "" : " const";
  std::string noexceptString = generateNoExcept( commandData.errorCodes, returnParams, vectorParams, flavourFlags, vectorSizeCheck.first, false );

  if ( definition )
  {
    std::string vectorSizeCheckString =
      vectorSizeCheck.first ? generateVectorSizeCheck( name, commandData, initialSkipCount, vectorSizeCheck.second, skippedParams, false ) : "";
    std::string returnVariable   = generateReturnVariable( commandData, returnParams, vectorParams, flavourFlags );
    std::string dataDeclarations = generateDataDeclarations(
      commandData, returnParams, vectorParams, templatedParams, flavourFlags, false, dataTypes, dataType, returnType, returnVariable );
    std::string dataPreparation =
      generateDataPreparation( commandData, initialSkipCount, returnParams, vectorParams, templatedParams, flavourFlags, enumerating );
    std::string dataSizeChecks = generateDataSizeChecks( commandData, returnParams, dataTypes, vectorParams, templatedParams, singular );
    std::string callSequence =
      generateCallSequence( name, commandData, returnParams, vectorParams, initialSkipCount, singularParams, templatedParams, flavourFlags, false );
    std::string resultCheck     = generateResultCheck( commandData, className, classSeparator, commandName, enumerating );
    std::string returnStatement = generateReturnStatement( name,
                                                           commandData,
                                                           returnVariable,
                                                           returnType,
                                                           dataType,
                                                           initialSkipCount,
                                                           returnParams.empty() ? INVALID_INDEX : returnParams[0],
                                                           flavourFlags,
                                                           enumerating,
                                                           false );

    std::string const functionTemplate =
      R"(  template <${argumentTemplates}${allocatorTemplates}typename Dispatch${uniqueHandleAllocatorTemplates}${typenameCheck}>
  ${nodiscard}VULKAN_HPP_INLINE ${returnType} ${className}${classSeparator}${commandName}( ${argumentList} )${const}${noexcept}
  {
    VULKAN_HPP_ASSERT( d.getVkHeaderVersion() == VK_HEADER_VERSION );
${vectorSizeCheck}
    ${dataSizeChecks}
    ${dataDeclarations}
    ${callSequence}
    ${resultCheck}
    ${dataPreparation}
    ${returnStatement}
  })";

    return replaceWithMap( functionTemplate,
                           { { "allocatorTemplates", allocatorTemplates },
                             { "argumentList", argumentList },
                             { "argumentTemplates", argumentTemplates },
                             { "callSequence", callSequence },
                             { "className", className },
                             { "classSeparator", classSeparator },
                             { "commandName", commandName },
                             { "const", constString },
                             { "dataDeclarations", dataDeclarations },
                             { "dataPreparation", dataPreparation },
                             { "dataSizeChecks", dataSizeChecks },
                             { "nodiscard", nodiscard },
                             { "noexcept", noexceptString },
                             { "resultCheck", resultCheck },
                             { "returnStatement", returnStatement },
                             { "returnType", returnType },
                             { "typenameCheck", typenameCheck },
                             { "uniqueHandleAllocatorTemplates", uniqueHandleAllocatorTemplates },
                             { "vectorSizeCheck", vectorSizeCheckString } } );
  }
  else
  {
    std::string const functionTemplate =
      R"(    template <${argumentTemplates}${allocatorTemplates}typename Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER_TYPE${uniqueHandleAllocatorTemplates}${typenameCheck}>
    ${nodiscard}${returnType} ${commandName}( ${argumentList} )${const}${noexcept};)";

    return replaceWithMap( functionTemplate,
                           { { "allocatorTemplates", allocatorTemplates },
                             { "argumentList", argumentList },
                             { "argumentTemplates", argumentTemplates },
                             { "commandName", commandName },
                             { "const", commandData.handle.empty() ? "" : " const" },
                             { "nodiscard", nodiscard },
                             { "noexcept", noexceptString },
                             { "returnType", returnType },
                             { "typenameCheck", typenameCheck },
                             { "uniqueHandleAllocatorTemplates", uniqueHandleAllocatorTemplates } } );
  }
}

std::string VulkanHppGenerator::generateCommandName( std::string const &            vulkanCommandName,
                                                     std::vector<ParamData> const & params,
                                                     size_t                         initialSkipCount,
                                                     CommandFlavourFlags            flavourFlags ) const
{
  std::string commandName( startLowerCase( stripPrefix( vulkanCommandName, "vk" ) ) );
  for ( size_t i = initialSkipCount - 1; i < initialSkipCount; --i )  // count down to zero, then wrap around and stop
  {
    std::string const & argumentType = params[i].type.type;
    std::string         searchName   = stripPrefix( argumentType, "Vk" );
    std::string         argumentTag  = findTag( argumentType );
    if ( !argumentTag.empty() )
    {
      searchName = stripPostfix( searchName, argumentTag );
    }
    size_t pos = commandName.find( searchName );
    if ( pos == std::string::npos )
    {
      searchName = startLowerCase( searchName );
      pos        = commandName.find( searchName );
    }
    if ( pos != std::string::npos )
    {
      size_t len = searchName.length();
      if ( commandName.find( searchName + "s" ) == pos )
      {
        // filter out any plural of the searchName as well!
        ++len;
      }
      commandName.erase( pos, len );
    }
    else if ( ( searchName == "commandBuffer" ) && commandName.starts_with( "cmd" ) )
    {
      commandName.erase( 0, 3 );
      pos = 0;
    }
    if ( pos == 0 )
    {
      commandName = startLowerCase( commandName );
    }
    std::string commandTag = findTag( commandName );
    if ( !argumentTag.empty() && ( argumentTag == commandTag ) )
    {
      commandName = stripPostfix( commandName, argumentTag );
    }
  }
  if ( flavourFlags & CommandFlavourFlagBits::singular )
  {
    commandName = stripPluralS( commandName );
  }
  if ( flavourFlags & CommandFlavourFlagBits::unique )
  {
    commandName += "Unique";
  }
  return commandName;
}

std::string VulkanHppGenerator::generateCommandResult(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  assert( !commandData.successCodes.empty() );
  if ( commandData.successCodes.size() == 1 )
  {
    return generateCommandResultSingleSuccess( name, commandData, initialSkipCount, definition, raii );
  }
  else if ( commandData.errorCodes.empty() )
  {
    return generateCommandResultMultiSuccessNoErrors( name, commandData, initialSkipCount, definition, raii );
  }
  else
  {
    return generateCommandResultMultiSuccessWithErrors( name, commandData, initialSkipCount, definition, raii );
  }
}

std::string VulkanHppGenerator::generateCommandResultMultiSuccessNoErrors(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::vector<size_t> returnParams = determineReturnParams( commandData.params );
  switch ( returnParams.size() )
  {
    case 0:
      {
        std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
        if ( vectorParams.empty() )
        {
          std::vector<size_t> constPointerParams = determineConstPointerParams( commandData.params );
          if ( constPointerParams.empty() )
          {
            return generateCommandSetExclusive( name, commandData, initialSkipCount, definition, raii );
          }
        }
      }
      break;
    case 2:
      if ( ( commandData.successCodes.size() == 2 ) && ( commandData.successCodes[0] == "VK_SUCCESS" ) && ( commandData.successCodes[1] == "VK_INCOMPLETE" ) )
      {
        if ( ( commandData.params[returnParams[0]].type.type == "size_t" ) || ( commandData.params[returnParams[0]].type.type == "uint32_t" ) )
        {
          if ( ( commandData.params[returnParams[1]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[1]].type.type ) &&
               !isStructureChainAnchor( commandData.params[returnParams[1]].type.type ) )
          {
            std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
            if ( vectorParams.size() == 1 )
            {
              if ( returnParams[0] == vectorParams.begin()->second.lenParam )
              {
                if ( returnParams[1] == vectorParams.begin()->first )
                {
                  return generateCommandSetInclusive( name,
                                                      commandData,
                                                      initialSkipCount,
                                                      definition,
                                                      returnParams,
                                                      vectorParams,
                                                      false,
                                                      { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                                      raii,
                                                      false,
                                                      { CommandFlavourFlagBits::enhanced } );
                }
              }
            }
          }
        }
      }
      break;
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultMultiSuccessWithErrors(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::vector<size_t> returnParams = determineReturnParams( commandData.params );
  switch ( returnParams.size() )
  {
    case 0: return generateCommandResultWithErrors0Return( name, commandData, initialSkipCount, definition, raii );
    case 1: return generateCommandResultMultiSuccessWithErrors1Return( name, commandData, initialSkipCount, definition, returnParams[0], raii );
    case 2: return generateCommandResultMultiSuccessWithErrors2Return( name, commandData, initialSkipCount, definition, returnParams, raii );
    case 3: return generateCommandResultMultiSuccessWithErrors3Return( name, commandData, initialSkipCount, definition, returnParams, raii );
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultMultiSuccessWithErrors1Return(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, size_t returnParam, bool raii ) const
{
  if ( commandData.params[returnParam].type.type == "void" )
  {
    std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
    if ( vectorParams.size() == 1 )
    {
      if ( returnParam == vectorParams.begin()->first )
      {
        if ( commandData.params[vectorParams.begin()->second.lenParam].type.isValue() )
        {
          return generateCommandSetInclusive( name,
                                              commandData,
                                              initialSkipCount,
                                              definition,
                                              { returnParam },
                                              vectorParams,
                                              false,
                                              { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular },
                                              raii,
                                              false,
                                              { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular } );
        }
      }
    }
  }
  else if ( isHandleType( commandData.params[returnParam].type.type ) )
  {
    std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
    if ( vectorParams.size() == 2 )
    {
      if ( returnParam == std::next( vectorParams.begin() )->first )
      {
        if ( vectorParams.begin()->second.lenParam == std::next( vectorParams.begin() )->second.lenParam )
        {
          if ( commandData.params[vectorParams.begin()->second.lenParam].type.type == "uint32_t" )
          {
            if ( isStructureChainAnchor( commandData.params[vectorParams.begin()->first].type.type ) )
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  { returnParam },
                                                  vectorParams,
                                                  true,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator, CommandFlavourFlagBits::singular },
                                                  raii,
                                                  true,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular } );
            }
          }
        }
      }
    }
  }
  else if ( isStructureChainAnchor( commandData.params[returnParam].type.type ) )
  {
    std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
    if ( vectorParams.empty() )
    {
#if 0
      // needs to be verified ...
      return generateCommandSetInclusive( name,
                                          commandData,
                                          initialSkipCount,
                                          definition,
                                          { returnParam },
                                          vectorParams,
                                          false,
                                          { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained },
                                          raii, false,
                                          { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained } );
#endif
      return "";
    }
  }
  else
  {
    std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
    if ( vectorParams.empty() )
    {
      return generateCommandSetInclusive( name,
                                          commandData,
                                          initialSkipCount,
                                          definition,
                                          { returnParam },
                                          vectorParams,
                                          false,
                                          { CommandFlavourFlagBits::enhanced },
                                          raii,
                                          false,
                                          { CommandFlavourFlagBits::enhanced } );
    }
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultMultiSuccessWithErrors2Return( std::string const &         name,
                                                                                    CommandData const &         commandData,
                                                                                    size_t                      initialSkipCount,
                                                                                    bool                        definition,
                                                                                    std::vector<size_t> const & returnParams,
                                                                                    bool                        raii ) const
{
  if ( ( commandData.successCodes.size() == 2 ) && ( commandData.successCodes[0] == "VK_SUCCESS" ) && ( commandData.successCodes[1] == "VK_INCOMPLETE" ) )
  {
    if ( ( commandData.params[returnParams[0]].type.type == "size_t" ) || ( commandData.params[returnParams[0]].type.type == "uint32_t" ) )
    {
      // needs some very special handling of "vkGetSwapchainImagesKHR" !!
      if ( isHandleType( commandData.params[returnParams[1]].type.type ) && ( name != "vkGetSwapchainImagesKHR" ) )
      {
        std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
        if ( vectorParams.size() == 1 )
        {
          if ( returnParams[0] == vectorParams.begin()->second.lenParam )
          {
            if ( returnParams[1] == vectorParams.begin()->first )
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  returnParams,
                                                  vectorParams,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                                  raii,
                                                  true,
                                                  { CommandFlavourFlagBits::enhanced } );
            }
          }
        }
      }
      else if ( isStructureChainAnchor( commandData.params[returnParams[1]].type.type ) )
      {
        std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
        if ( vectorParams.size() == 1 )
        {
          if ( returnParams[0] == vectorParams.begin()->second.lenParam )
          {
            if ( returnParams[1] == vectorParams.begin()->first )
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  returnParams,
                                                  vectorParams,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced,
                                                    CommandFlavourFlagBits::withAllocator,
                                                    CommandFlavourFlagBits::chained,
                                                    CommandFlavourFlagBits::chained | CommandFlavourFlagBits::withAllocator },
                                                  raii,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained } );
            }
          }
        }
      }
      else
      {
        std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
        if ( vectorParams.size() == 1 )
        {
          if ( returnParams[0] == vectorParams.begin()->second.lenParam )
          {
            if ( returnParams[1] == vectorParams.begin()->first )
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  returnParams,
                                                  vectorParams,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                                  raii,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced } );
            }
          }
        }
      }
    }
    else if ( ( commandData.params[returnParams[0]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[0]].type.type ) &&
              !isStructureChainAnchor( commandData.params[returnParams[0]].type.type ) )
    {
      if ( ( commandData.params[returnParams[1]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[1]].type.type ) &&
           !isStructureChainAnchor( commandData.params[returnParams[1]].type.type ) )
      {
        std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
        if ( vectorParams.empty() )
        {
          return generateCommandSetInclusive( name,
                                              commandData,
                                              initialSkipCount,
                                              definition,
                                              returnParams,
                                              vectorParams,
                                              false,
                                              { CommandFlavourFlagBits::enhanced },
                                              raii,
                                              false,
                                              { CommandFlavourFlagBits::enhanced } );
        }
      }
    }
  }

  return "";
}

std::string VulkanHppGenerator::generateCommandResultMultiSuccessWithErrors3Return( std::string const &         name,
                                                                                    CommandData const &         commandData,
                                                                                    size_t                      initialSkipCount,
                                                                                    bool                        definition,
                                                                                    std::vector<size_t> const & returnParams,
                                                                                    bool                        raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  switch ( vectorParams.size() )
  {
    case 1:
      if ( commandData.params[returnParams[0]].type.type == "VkBool32" )
      {
        if ( commandData.params[returnParams[1]].type.type == "uint32_t" )
        {
          if ( ( commandData.params[returnParams[2]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[2]].type.type ) &&
               !isStructureChainAnchor( commandData.params[returnParams[2]].type.type ) )
          {
            if ( returnParams[1] == vectorParams.begin()->second.lenParam )
            {
              if ( returnParams[2] == vectorParams.begin()->first )
              {
                return generateCommandSetInclusive( name,
                                                    commandData,
                                                    initialSkipCount,
                                                    definition,
                                                    returnParams,
                                                    vectorParams,
                                                    false,
                                                    { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                                    raii,
                                                    false,
                                                    { CommandFlavourFlagBits::enhanced } );
              }
            }
          }
        }
      }
      break;
    case 2:
      if ( commandData.params[returnParams[0]].type.type == "uint32_t" )
      {
        if ( ( commandData.params[returnParams[1]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[1]].type.type ) &&
             !isStructureChainAnchor( commandData.params[returnParams[1]].type.type ) )
        {
          if ( ( commandData.params[returnParams[2]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[2]].type.type ) &&
               !isStructureChainAnchor( commandData.params[returnParams[2]].type.type ) )
          {
            if ( vectorParams.begin()->second.lenParam == std::next( vectorParams.begin() )->second.lenParam )
            {
              if ( returnParams[0] == vectorParams.begin()->second.lenParam )
              {
                if ( returnParams[1] == vectorParams.begin()->first )
                {
                  if ( returnParams[2] == std::next( vectorParams.begin() )->first )
                  {
                    return generateCommandSetInclusive( name,
                                                        commandData,
                                                        initialSkipCount,
                                                        definition,
                                                        returnParams,
                                                        vectorParams,
                                                        false,
                                                        { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                                        raii,
                                                        false,
                                                        { CommandFlavourFlagBits::enhanced } );
                  }
                }
              }
            }
          }
        }
      }
      break;
    default: assert( false ); break;
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccess(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  if ( commandData.errorCodes.empty() )
  {
    return generateCommandResultSingleSuccessNoErrors( name, commandData, initialSkipCount, definition, raii );
  }
  else
  {
    return generateCommandResultSingleSuccessWithErrors( name, commandData, initialSkipCount, definition, raii );
  }
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessNoErrors(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::vector<size_t> returnParams = determineReturnParams( commandData.params );
  switch ( returnParams.size() )
  {
    case 0:
      {
        std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
        switch ( vectorParams.size() )
        {
          case 0:
            if ( determineConstPointerParams( commandData.params ).empty() )
            {
              return generateCommandSetExclusive( name, commandData, initialSkipCount, definition, raii );
            }
            else
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  returnParams,
                                                  vectorParams,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced },
                                                  raii,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced } );
            }
            break;
          case 1:
            if ( !raii )
            {
              if ( commandData.params[vectorParams.begin()->second.lenParam].type.isValue() )
              {
                if ( isHandleType( commandData.params[vectorParams.begin()->first].type.type ) )
                {
                  return generateCommandSet( definition,
                                             generateCommandStandard( name, commandData, initialSkipCount, definition ),
                                             { generateCommandEnhanced( name, commandData, initialSkipCount, definition, vectorParams, {} ) } );
                }
              }
            }
            break;
        }
      }
      break;
    case 1:
      {
        std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
        if ( vectorParams.empty() )
        {
          if ( ( commandData.params[returnParams[0]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[0]].type.type ) &&
               !isStructureChainAnchor( commandData.params[returnParams[0]].type.type ) )
          {
            return generateCommandSetInclusive( name,
                                                commandData,
                                                initialSkipCount,
                                                definition,
                                                returnParams,
                                                vectorParams,
                                                false,
                                                { CommandFlavourFlagBits::enhanced },
                                                raii,
                                                false,
                                                { CommandFlavourFlagBits::enhanced } );
          }
        }
      }
      break;
    case 2:
      if ( ( commandData.params[returnParams[0]].type.type == "size_t" ) || ( commandData.params[returnParams[0]].type.type == "uint32_t" ) )
      {
        if ( ( commandData.params[returnParams[1]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[1]].type.type ) &&
             !isStructureChainAnchor( commandData.params[returnParams[1]].type.type ) )
        {
          std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
          if ( vectorParams.size() == 1 )
          {
            if ( returnParams[0] == vectorParams.begin()->second.lenParam )
            {
              if ( returnParams[1] == vectorParams.begin()->first )
              {
                return generateCommandSetInclusive( name,
                                                    commandData,
                                                    initialSkipCount,
                                                    definition,
                                                    returnParams,
                                                    vectorParams,
                                                    false,
                                                    { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                                    raii,
                                                    false,
                                                    { CommandFlavourFlagBits::enhanced } );
              }
            }
          }
        }
      }
      break;
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessWithErrors(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::vector<size_t> returnParams = determineReturnParams( commandData.params );
  switch ( returnParams.size() )
  {
    case 0: return generateCommandResultWithErrors0Return( name, commandData, initialSkipCount, definition, raii );
    case 1: return generateCommandResultSingleSuccessWithErrors1Return( name, commandData, initialSkipCount, definition, returnParams[0], raii );
    case 2: return generateCommandResultSingleSuccessWithErrors2Return( name, commandData, initialSkipCount, definition, returnParams, raii );
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessWithErrors1Return(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, size_t returnParam, bool raii ) const
{
  if ( commandData.params[returnParam].type.type == "void" )
  {
    return generateCommandResultSingleSuccessWithErrors1ReturnVoid( name, commandData, initialSkipCount, definition, returnParam, raii );
  }
  else if ( isHandleType( commandData.params[returnParam].type.type ) )
  {
    return generateCommandResultSingleSuccessWithErrors1ReturnHandle( name, commandData, initialSkipCount, definition, returnParam, raii );
  }
  else if ( isStructureChainAnchor( commandData.params[returnParam].type.type ) )
  {
    return generateCommandResultSingleSuccessWithErrors1ReturnChain( name, commandData, initialSkipCount, definition, returnParam, raii );
  }
  else
  {
    return generateCommandResultSingleSuccessWithErrors1ReturnValue( name, commandData, initialSkipCount, definition, returnParam, raii );
  }
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessWithErrors1ReturnChain(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, size_t returnParam, bool raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  if ( vectorParams.empty() )
  {
    return generateCommandSetInclusive( name,
                                        commandData,
                                        initialSkipCount,
                                        definition,
                                        { returnParam },
                                        vectorParams,
                                        false,
                                        { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained },
                                        raii,
                                        false,
                                        { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained } );
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessWithErrors1ReturnHandle(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, size_t returnParam, bool raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  switch ( vectorParams.size() )
  {
    case 0:
      return generateCommandSetInclusive( name,
                                          commandData,
                                          initialSkipCount,
                                          definition,
                                          { returnParam },
                                          vectorParams,
                                          true,
                                          { CommandFlavourFlagBits::enhanced },
                                          raii,
                                          true,
                                          { CommandFlavourFlagBits::enhanced } );
    case 1:
      if ( returnParam == vectorParams.begin()->first )
      {
        if ( isLenByStructMember( commandData.params[vectorParams.begin()->first].lenExpression, commandData.params[vectorParams.begin()->second.lenParam] ) )
        {
          return generateCommandSetInclusive( name,
                                              commandData,
                                              initialSkipCount,
                                              definition,
                                              { returnParam },
                                              vectorParams,
                                              true,
                                              { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                              raii,
                                              true,
                                              { CommandFlavourFlagBits::enhanced } );
        }
      }
      break;
    case 2:
      if ( returnParam == std::next( vectorParams.begin() )->first )
      {
        if ( vectorParams.begin()->second.lenParam == std::next( vectorParams.begin() )->second.lenParam )
        {
          if ( commandData.params[vectorParams.begin()->second.lenParam].type.isValue() )
          {
            if ( ( commandData.params[vectorParams.begin()->first].type.type != "void" ) &&
                 !isHandleType( commandData.params[vectorParams.begin()->first].type.type ) )
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  { returnParam },
                                                  vectorParams,
                                                  true,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator, CommandFlavourFlagBits::singular },
                                                  raii,
                                                  true,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular } );
            }
          }
        }
      }
      break;
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessWithErrors1ReturnValue(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, size_t returnParam, bool raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  switch ( vectorParams.size() )
  {
    case 0:
      return generateCommandSetInclusive( name,
                                          commandData,
                                          initialSkipCount,
                                          definition,
                                          { returnParam },
                                          vectorParams,
                                          false,
                                          { CommandFlavourFlagBits::enhanced },
                                          raii,
                                          false,
                                          { CommandFlavourFlagBits::enhanced } );
    case 2:
      if ( returnParam == std::next( vectorParams.begin() )->first )
      {
        if ( vectorParams.begin()->second.lenParam == std::next( vectorParams.begin() )->second.lenParam )
        {
          if ( commandData.params[vectorParams.begin()->second.lenParam].type.type == "uint32_t" )
          {
            if ( ( commandData.params[vectorParams.begin()->first].type.type != "void" ) &&
                 !isHandleType( commandData.params[vectorParams.begin()->first].type.type ) &&
                 !isStructureChainAnchor( commandData.params[vectorParams.begin()->first].type.type ) )
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  { returnParam },
                                                  vectorParams,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator, CommandFlavourFlagBits::singular },
                                                  raii,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular } );
            }
          }
        }
      }
      break;
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessWithErrors1ReturnVoid(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, size_t returnParam, bool raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  switch ( vectorParams.size() )
  {
    case 0:
      return generateCommandSetInclusive( name,
                                          commandData,
                                          initialSkipCount,
                                          definition,
                                          { returnParam },
                                          vectorParams,
                                          false,
                                          { CommandFlavourFlagBits::enhanced },
                                          raii,
                                          false,
                                          { CommandFlavourFlagBits::enhanced } );
    case 1:
      if ( returnParam == vectorParams.begin()->first )
      {
        if ( commandData.params[vectorParams.begin()->second.lenParam].type.isValue() )
        {
          return generateCommandSetInclusive( name,
                                              commandData,
                                              initialSkipCount,
                                              definition,
                                              { returnParam },
                                              vectorParams,
                                              false,
                                              { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular },
                                              raii,
                                              false,
                                              { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular } );
        }
      }
      break;
    case 2:
      if ( returnParam == std::next( vectorParams.begin() )->first )
      {
        if ( vectorParams.begin()->second.lenParam != std::next( vectorParams.begin() )->second.lenParam )
        {
          if ( commandData.params[vectorParams.begin()->second.lenParam].type.isValue() )
          {
            if ( isHandleType( commandData.params[vectorParams.begin()->first].type.type ) )
            {
              if ( commandData.params[std::next( vectorParams.begin() )->second.lenParam].type.isValue() )
              {
                return generateCommandSetInclusive( name,
                                                    commandData,
                                                    initialSkipCount,
                                                    definition,
                                                    { returnParam },
                                                    vectorParams,
                                                    false,
                                                    { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular },
                                                    raii,
                                                    false,
                                                    { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular } );
              }
            }
          }
        }
      }
      break;
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultSingleSuccessWithErrors2Return( std::string const &         name,
                                                                                     CommandData const &         commandData,
                                                                                     size_t                      initialSkipCount,
                                                                                     bool                        definition,
                                                                                     std::vector<size_t> const & returnParams,
                                                                                     bool                        raii ) const
{
  if ( ( commandData.params[returnParams[0]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[0]].type.type ) &&
       !isStructureChainAnchor( commandData.params[returnParams[0]].type.type ) )
  {
    if ( ( commandData.params[returnParams[1]].type.type != "void" ) && !isHandleType( commandData.params[returnParams[1]].type.type ) &&
         !isStructureChainAnchor( commandData.params[returnParams[1]].type.type ) )
    {
      std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
      if ( vectorParams.size() == 2 )
      {
        if ( returnParams[0] == std::next( vectorParams.begin() )->first )
        {
          if ( vectorParams.find( returnParams[1] ) == vectorParams.end() )
          {
            assert( ( returnParams[1] != vectorParams.begin()->second.lenParam ) && ( returnParams[1] != std::next( vectorParams.begin() )->second.lenParam ) );
            if ( vectorParams.begin()->second.lenParam == std::next( vectorParams.begin() )->second.lenParam )
            {
              if ( commandData.params[vectorParams.begin()->second.lenParam].type.isValue() )
              {
                if ( ( commandData.params[vectorParams.begin()->first].type.type != "void" ) &&
                     !isHandleType( commandData.params[vectorParams.begin()->first].type.type ) &&
                     !isStructureChainAnchor( commandData.params[vectorParams.begin()->first].type.type ) )
                {
                  return generateCommandSetInclusive(
                    name,
                    commandData,
                    initialSkipCount,
                    definition,
                    returnParams,
                    vectorParams,
                    false,
                    { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator, CommandFlavourFlagBits::singular },
                    raii,
                    false,
                    { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::singular } );
                }
              }
            }
          }
        }
      }
    }
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandResultWithErrors0Return(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  if ( vectorParams.empty() && determineConstPointerParams( commandData.params ).empty() )
  {
    return generateCommandSetExclusive( name, commandData, initialSkipCount, definition, raii );
  }
  else if ( allVectorSizesSupported( commandData.params, vectorParams ) )
  {
    return generateCommandSetInclusive( name,
                                        commandData,
                                        initialSkipCount,
                                        definition,
                                        {},
                                        vectorParams,
                                        false,
                                        { CommandFlavourFlagBits::enhanced },
                                        raii,
                                        false,
                                        { CommandFlavourFlagBits::enhanced } );
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandSet( bool                             definition,
                                                    std::string const &              standard,
                                                    std::vector<std::string> const & enhanced,
                                                    std::vector<std::string> const & unique ) const
{
  assert( unique.empty() || ( enhanced.size() == unique.size() ) );

  std::string commandSet = "\n" + standard;
  if ( !enhanced.empty() )
  {
    std::string separator = definition ? "\n" : "";
    commandSet += separator + "\n#ifndef VULKAN_HPP_DISABLE_ENHANCED_MODE\n";
    bool firstEnhanced = true;
    for ( auto const & e : enhanced )
    {
      if ( !firstEnhanced )
      {
        commandSet += separator + "\n";
      }
      firstEnhanced = false;
      commandSet += e;
    }
    if ( !unique.empty() )
    {
      commandSet += separator + "\n#  ifndef VULKAN_HPP_NO_SMART_HANDLE\n";
      bool firstUnique = true;
      for ( auto const & u : unique )
      {
        if ( !firstUnique )
        {
          commandSet += separator + "\n";
        }
        firstUnique = false;
        commandSet += u;
      }
      commandSet += "\n#  endif /* VULKAN_HPP_NO_SMART_HANDLE */";
    }
    commandSet += "\n#endif /* VULKAN_HPP_DISABLE_ENHANCED_MODE */";
  }
  commandSet += "\n";
  return commandSet;
}

std::string VulkanHppGenerator::generateCommandSet( std::string const & standard, std::string const & enhanced ) const
{
  const std::string commandTemplate = R"(
#ifdef VULKAN_HPP_DISABLE_ENHANCED_MODE
${commandStandard}
#else
${commandEnhanced}
#endif /*VULKAN_HPP_DISABLE_ENHANCED_MODE*/
)";

  return replaceWithMap( commandTemplate, std::map<std::string, std::string>( { { "commandEnhanced", enhanced }, { "commandStandard", standard } } ) );
}

std::string VulkanHppGenerator::generateCommandSetExclusive(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  if ( raii )
  {
    return generateRAIIHandleCommandEnhanced( name, commandData, initialSkipCount, {}, {}, definition );
  }
  else
  {
    return generateCommandSet( generateCommandStandard( name, commandData, initialSkipCount, definition ),
                               generateCommandEnhanced( name, commandData, initialSkipCount, definition, {}, {} ) );
  }
}

std::string VulkanHppGenerator::generateCommandSetInclusive( std::string const &                      name,
                                                             CommandData const &                      commandData,
                                                             size_t                                   initialSkipCount,
                                                             bool                                     definition,
                                                             std::vector<size_t>                      returnParams,
                                                             std::map<size_t, VectorParamData>        vectorParams,
                                                             bool                                     unique,
                                                             std::vector<CommandFlavourFlags> const & flags,
                                                             bool                                     raii,
                                                             bool                                     raiiFactory,
                                                             std::vector<CommandFlavourFlags> const & raiiFlags ) const
{
  if ( raii )
  {
    std::string raiiCommands;
    for ( auto flag : raiiFlags )
    {
      raiiCommands += raiiFactory ? generateRAIIHandleCommandFactory( name, commandData, initialSkipCount, returnParams, vectorParams, definition, flag )
                                  : generateRAIIHandleCommandEnhanced( name, commandData, initialSkipCount, returnParams, vectorParams, definition, flag );
    }
    return raiiCommands;
  }
  else
  {
    std::vector<std::string> enhancedCommands, uniqueCommands;
    for ( auto flag : flags )
    {
      enhancedCommands.push_back( generateCommandEnhanced( name, commandData, initialSkipCount, definition, vectorParams, returnParams, flag ) );
      if ( unique )
      {
        uniqueCommands.push_back(
          generateCommandEnhanced( name, commandData, initialSkipCount, definition, vectorParams, returnParams, flag | CommandFlavourFlagBits::unique ) );
      }
    }
    return generateCommandSet( definition, generateCommandStandard( name, commandData, initialSkipCount, definition ), enhancedCommands, uniqueCommands );
  }
}

std::string
  VulkanHppGenerator::generateCommandStandard( std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition ) const
{
  std::set<size_t> skippedParams = determineSkippedParams( commandData.params, initialSkipCount, {}, {}, false );

  std::string argumentList = generateArgumentListStandard( commandData.params, skippedParams );
  std::string commandName  = generateCommandName( name, commandData.params, initialSkipCount );
  std::string nodiscard    = ( 1 < commandData.successCodes.size() + commandData.errorCodes.size() ) ? "VULKAN_HPP_NODISCARD " : "";
  std::string returnType   = stripPrefix( commandData.returnType, "Vk" );

  if ( definition )
  {
    std::string functionBody = "d." + name + "( " + generateCallArgumentsStandard( commandData.handle, commandData.params ) + " )";
    if ( commandData.returnType.starts_with( "Vk" ) )
    {
      functionBody = "return static_cast<" + returnType + ">( " + functionBody + " )";
    }
    else if ( commandData.returnType != "void" )
    {
      functionBody = "return " + functionBody;
    }

    std::string const functionTemplate =
      R"(  template <typename Dispatch>
  ${nodiscard}VULKAN_HPP_INLINE ${returnType} ${className}${classSeparator}${commandName}( ${argumentList} )${const} VULKAN_HPP_NOEXCEPT
  {
    VULKAN_HPP_ASSERT( d.getVkHeaderVersion() == VK_HEADER_VERSION );
    ${functionBody};
  })";

    return replaceWithMap( functionTemplate,
                           { { "argumentList", argumentList },
                             { "className", initialSkipCount ? stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" ) : "" },
                             { "classSeparator", commandData.handle.empty() ? "" : "::" },
                             { "commandName", commandName },
                             { "const", commandData.handle.empty() ? "" : " const" },
                             { "functionBody", functionBody },
                             { "nodiscard", nodiscard },
                             { "returnType", returnType } } );
  }
  else
  {
    std::string const functionTemplate =
      R"(    template <typename Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
    ${nodiscard}${returnType} ${commandName}( ${argumentList} VULKAN_HPP_DEFAULT_DISPATCHER_ASSIGNMENT )${const} VULKAN_HPP_NOEXCEPT;)";

    return replaceWithMap( functionTemplate,
                           { { "argumentList", argumentList },
                             { "commandName", commandName },
                             { "const", commandData.handle.empty() ? "" : " const" },
                             { "nodiscard", nodiscard },
                             { "returnType", returnType } } );
  }
}

std::string VulkanHppGenerator::generateCommandVoid(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::vector<size_t> returnParams = determineReturnParams( commandData.params );
  switch ( returnParams.size() )
  {
    case 0: return generateCommandVoid0Return( name, commandData, initialSkipCount, definition, raii );
    case 1: return generateCommandVoid1Return( name, commandData, initialSkipCount, definition, returnParams[0], raii );
    case 2: return generateCommandVoid2Return( name, commandData, initialSkipCount, definition, returnParams, raii );
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandValue(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::vector<size_t> returnParams = determineReturnParams( commandData.params );
  if ( returnParams.empty() )
  {
    std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
    if ( vectorParams.empty() )
    {
      if ( determineConstPointerParams( commandData.params ).empty() )
      {
        return generateCommandSetInclusive(
          name, commandData, initialSkipCount, definition, {}, vectorParams, false, {}, raii, false, { CommandFlavourFlagBits::enhanced } );
      }
      else
      {
        return generateCommandSetInclusive( name,
                                            commandData,
                                            initialSkipCount,
                                            definition,
                                            {},
                                            vectorParams,
                                            false,
                                            { CommandFlavourFlagBits::enhanced },
                                            raii,
                                            false,
                                            { CommandFlavourFlagBits::enhanced } );
      }
    }
    else if ( vectorParams.size() <= 1 )
    {
      if ( !raii )
      {
        return generateCommandSet( definition,
                                   generateCommandStandard( name, commandData, initialSkipCount, definition ),
                                   { generateCommandEnhanced( name, commandData, initialSkipCount, definition, vectorParams, returnParams ) } );
      }
    }
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandVoid0Return(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, bool raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  if ( vectorParams.empty() && determineConstPointerParams( commandData.params ).empty() )
  {
    return generateCommandSetInclusive(
      name, commandData, initialSkipCount, definition, {}, vectorParams, false, {}, raii, false, { CommandFlavourFlagBits::enhanced } );
  }
  else if ( allVectorSizesSupported( commandData.params, vectorParams ) )
  {
    // All the vectorParams have a counter by value, of type "uint32_t", "VkDeviceSize", or "VkSampleCountFlagBits" (!)
    return generateCommandSetInclusive( name,
                                        commandData,
                                        initialSkipCount,
                                        definition,
                                        {},
                                        vectorParams,
                                        false,
                                        { CommandFlavourFlagBits::enhanced },
                                        raii,
                                        false,
                                        { CommandFlavourFlagBits::enhanced } );
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandVoid1Return(
  std::string const & name, CommandData const & commandData, size_t initialSkipCount, bool definition, size_t returnParam, bool raii ) const
{
  std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
  if ( ( commandData.params[returnParam].type.type == "void" ) )
  {
    switch ( vectorParams.size() )
    {
      case 0:
        return generateCommandSetInclusive( name,
                                            commandData,
                                            initialSkipCount,
                                            definition,
                                            { returnParam },
                                            vectorParams,
                                            false,
                                            { CommandFlavourFlagBits::enhanced },
                                            raii,
                                            false,
                                            { CommandFlavourFlagBits::enhanced } );
      case 1:
        if ( returnParam == vectorParams.begin()->first )
        {
          if ( name == stripPluralS( name ) )
          {
            return generateCommandSetInclusive( name,
                                                commandData,
                                                initialSkipCount,
                                                definition,
                                                { returnParam },
                                                vectorParams,
                                                false,
                                                { CommandFlavourFlagBits::singular },
                                                raii,
                                                false,
                                                { CommandFlavourFlagBits::singular } );
          }
        }
        break;
    }
  }
  else if ( isHandleType( commandData.params[returnParam].type.type ) )
  {
    if ( vectorParams.empty() )
    {
      return generateCommandSetInclusive( name,
                                          commandData,
                                          initialSkipCount,
                                          definition,
                                          { returnParam },
                                          vectorParams,
                                          false,
                                          { CommandFlavourFlagBits::enhanced },
                                          raii,
                                          true,
                                          { CommandFlavourFlagBits::enhanced } );
    }
  }
  else if ( isStructureChainAnchor( commandData.params[returnParam].type.type ) )
  {
    if ( vectorParams.empty() )
    {
      return generateCommandSetInclusive( name,
                                          commandData,
                                          initialSkipCount,
                                          definition,
                                          { returnParam },
                                          vectorParams,
                                          false,
                                          { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained },
                                          raii,
                                          false,
                                          { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained } );
    }
  }
  else
  {
    switch ( vectorParams.size() )
    {
      case 0:
        return generateCommandSetInclusive( name,
                                            commandData,
                                            initialSkipCount,
                                            definition,
                                            { returnParam },
                                            vectorParams,
                                            false,
                                            { CommandFlavourFlagBits::enhanced },
                                            raii,
                                            false,
                                            { CommandFlavourFlagBits::enhanced } );
      case 1:
        if ( returnParam == vectorParams.begin()->first )
        {
          if ( !raii )
          {
            // you get a vector of stuff, with the size being one of the parameters
            return generateCommandSet(
              definition,
              generateCommandStandard( name, commandData, initialSkipCount, definition ),
              { generateCommandEnhanced( name, commandData, initialSkipCount, definition, vectorParams, { returnParam } ),
                generateCommandEnhanced(
                  name, commandData, initialSkipCount, definition, vectorParams, { returnParam }, CommandFlavourFlagBits::withAllocator ) } );
          }
        }
        else
        {
          if ( !isHandleType( commandData.params[vectorParams.begin()->first].type.type ) &&
               !isStructureChainAnchor( commandData.params[vectorParams.begin()->first].type.type ) &&
               ( commandData.params[vectorParams.begin()->first].type.type != "void" ) )
          {
            if ( isLenByStructMember( commandData.params[vectorParams.begin()->first].lenExpression,
                                      commandData.params[vectorParams.begin()->second.lenParam] ) )
            {
              return generateCommandSetInclusive( name,
                                                  commandData,
                                                  initialSkipCount,
                                                  definition,
                                                  { returnParam },
                                                  vectorParams,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced },
                                                  raii,
                                                  false,
                                                  { CommandFlavourFlagBits::enhanced } );
            }
          }
        }
        break;
    }
  }
  return "";
}

std::string VulkanHppGenerator::generateCommandVoid2Return( std::string const &         name,
                                                            CommandData const &         commandData,
                                                            size_t                      initialSkipCount,
                                                            bool                        definition,
                                                            std::vector<size_t> const & returnParams,
                                                            bool                        raii ) const
{
  if ( commandData.params[returnParams[0]].type.type == "uint32_t" )
  {
    std::map<size_t, VectorParamData> vectorParams = determineVectorParams( commandData.params );
    if ( vectorParams.size() == 1 )
    {
      if ( returnParams[0] == vectorParams.begin()->second.lenParam )
      {
        if ( returnParams[1] == vectorParams.begin()->first )
        {
          if ( isStructureChainAnchor( commandData.params[returnParams[1]].type.type ) )
          {
            return generateCommandSetInclusive( name,
                                                commandData,
                                                initialSkipCount,
                                                definition,
                                                returnParams,
                                                vectorParams,
                                                false,
                                                { CommandFlavourFlagBits::enhanced,
                                                  CommandFlavourFlagBits::withAllocator,
                                                  CommandFlavourFlagBits::chained,
                                                  CommandFlavourFlagBits::chained | CommandFlavourFlagBits::withAllocator },
                                                raii,
                                                false,
                                                { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::chained } );
          }
          else
          {
            return generateCommandSetInclusive( name,
                                                commandData,
                                                initialSkipCount,
                                                definition,
                                                returnParams,
                                                vectorParams,
                                                false,
                                                { CommandFlavourFlagBits::enhanced, CommandFlavourFlagBits::withAllocator },
                                                raii,
                                                false,
                                                { CommandFlavourFlagBits::enhanced } );
          }
        }
      }
    }
  }
  return "";
}

std::string VulkanHppGenerator::generateConstexprString( std::string const & structName ) const
{
  // structs with a VkBaseInStructure and VkBaseOutStructure can't be a constexpr!
  bool isConstExpression = ( structName != "VkBaseInStructure" ) && ( structName != "VkBaseOutStructure" );
  return isConstExpression ? ( std::string( "VULKAN_HPP_CONSTEXPR" ) + ( ( containsUnion( structName ) || containsArray( structName ) ) ? "_14 " : " " ) ) : "";
}

std::string VulkanHppGenerator::generateDataDeclarations( CommandData const &                       commandData,
                                                          std::vector<size_t> const &               returnParams,
                                                          std::map<size_t, VectorParamData> const & vectorParams,
                                                          std::set<size_t> const &                  templatedParams,
                                                          CommandFlavourFlags                       flavourFlags,
                                                          bool                                      raii,
                                                          std::vector<std::string> const &          dataTypes,
                                                          std::string const &                       dataType,
                                                          std::string const &                       returnType,
                                                          std::string const &                       returnVariable ) const
{
  assert( dataTypes.size() == returnParams.size() );

  switch ( returnParams.size() )
  {
    case 0: return "";  // no returnParams -> no data declarations
    case 1:
      return generateDataDeclarations1Return(
        commandData, returnParams, vectorParams, templatedParams, flavourFlags, dataTypes, dataType, returnType, returnVariable );
    case 2:
      assert( !( flavourFlags & CommandFlavourFlagBits::unique ) );
      return generateDataDeclarations2Returns( commandData, returnParams, vectorParams, flavourFlags, raii, dataTypes, dataType, returnVariable );
    case 3:
      assert( templatedParams.empty() &&
              !( flavourFlags & ( CommandFlavourFlagBits::chained | CommandFlavourFlagBits::singular | CommandFlavourFlagBits::unique ) ) );
      return generateDataDeclarations3Returns( commandData, returnParams, vectorParams, flavourFlags, raii, dataTypes );
    default: assert( false ); return "";
  }
}

std::string VulkanHppGenerator::generateDataDeclarations1Return( CommandData const &                       commandData,
                                                                 std::vector<size_t> const &               returnParams,
                                                                 std::map<size_t, VectorParamData> const & vectorParams,
                                                                 std::set<size_t> const &                  templatedParams,
                                                                 CommandFlavourFlags                       flavourFlags,
                                                                 std::vector<std::string> const &          dataTypes,
                                                                 std::string const &                       dataType,
                                                                 std::string const &                       returnType,
                                                                 std::string const &                       returnVariable ) const
{
  auto vectorParamIt = vectorParams.find( returnParams[0] );
  if ( !( flavourFlags & CommandFlavourFlagBits::chained ) )
  {
    if ( ( vectorParamIt == vectorParams.end() ) || ( flavourFlags & CommandFlavourFlagBits::singular ) )
    {
      std::string const dataDeclarationsTemplate = R"(${returnType} ${returnVariable};)";

      return replaceWithMap( dataDeclarationsTemplate, { { "returnType", dataType }, { "returnVariable", returnVariable } } );
    }
    else
    {
      std::string allocator       = stripPrefix( dataTypes[0], "VULKAN_HPP_NAMESPACE::" ) + "Allocator";
      std::string vectorAllocator = ( ( flavourFlags & CommandFlavourFlagBits::withAllocator ) && !( flavourFlags & CommandFlavourFlagBits::unique ) )
                                    ? ( ", " + startLowerCase( allocator ) )
                                    : "";
      std::string vectorSize      = getVectorSize( commandData.params, vectorParams, returnParams[0], dataTypes[0], templatedParams );

      std::string const dataDeclarationsTemplate = R"(${dataType} ${returnVariable}( ${vectorSize}${vectorAllocator} );)";

      return replaceWithMap(
        dataDeclarationsTemplate,
        { { "dataType", dataType }, { "returnVariable", returnVariable }, { "vectorAllocator", vectorAllocator }, { "vectorSize", vectorSize } } );
    }
  }
  else
  {
    assert( ( vectorParamIt == vectorParams.end() ) || ( flavourFlags & CommandFlavourFlagBits::singular ) );

    std::string dataVariable = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );

    std::string const dataDeclarationsTemplate = R"(${returnType} ${returnVariable};
    ${dataType} & ${dataVariable} = ${returnVariable}.template get<${dataType}>();)";

    return replaceWithMap( dataDeclarationsTemplate,
                           { { "dataType", dataTypes[0] },
                             { "dataVariable", dataVariable },
                             { "returnType", ( commandData.returnType == "void" ) ? returnType : "StructureChain<X, Y, Z...>" },
                             { "returnVariable", returnVariable } } );
  }
}

std::string VulkanHppGenerator::generateDataDeclarations2Returns( CommandData const &                       commandData,
                                                                  std::vector<size_t> const &               returnParams,
                                                                  std::map<size_t, VectorParamData> const & vectorParams,
                                                                  CommandFlavourFlags                       flavourFlags,
                                                                  bool                                      raii,
                                                                  std::vector<std::string> const &          dataTypes,
                                                                  std::string const &                       dataType,
                                                                  std::string const &                       returnVariable ) const
{
  bool chained       = flavourFlags & CommandFlavourFlagBits::chained;
  bool singular      = flavourFlags & CommandFlavourFlagBits::singular;
  bool withAllocator = flavourFlags & CommandFlavourFlagBits::withAllocator;

  switch ( vectorParams.size() )
  {
    case 0:
      assert( !singular && !chained );
      {
        std::string firstDataVariable  = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );
        std::string secondDataVariable = startLowerCase( stripPrefix( commandData.params[returnParams[1]].name, "p" ) );

        std::string const dataDeclarationTemplate = R"(std::pair<${firstDataType},${secondDataType}> data;
    ${firstDataType} & ${firstDataVariable} = data.first;
    ${secondDataType} & ${secondDataVariable} = data.second;)";

        return replaceWithMap( dataDeclarationTemplate,
                               { { "firstDataType", dataTypes[0] },
                                 { "firstDataVariable", firstDataVariable },
                                 { "secondDataType", dataTypes[1] },
                                 { "secondDataVariable", secondDataVariable } } );
      }
    case 1:
      assert( ( returnParams[0] == vectorParams.begin()->second.lenParam ) && ( returnParams[1] == vectorParams.begin()->first ) && !singular );
      {
        std::string counterVariable = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );
        if ( !chained )
        {
          std::string vectorAllocator = withAllocator ? ( "( " + startLowerCase( stripPrefix( dataTypes[1], "VULKAN_HPP_NAMESPACE::" ) ) + "Allocator )" ) : "";

          std::string const dataDeclarationTemplate = R"(${returnType} ${returnVariable}${vectorAllocator};
    ${counterType} ${counterVariable};)";

          return replaceWithMap( dataDeclarationTemplate,
                                 { { "counterType", dataTypes[0] },
                                   { "counterVariable", counterVariable },
                                   { "returnType", dataType },
                                   { "returnVariable", returnVariable },
                                   { "vectorAllocator", vectorAllocator } } );
        }
        else
        {
          std::string structureChainAllocator   = raii ? "" : ", StructureChainAllocator";
          std::string structureChainInitializer = withAllocator ? ( "( structureChainAllocator )" ) : "";
          std::string vectorVariable            = startLowerCase( stripPrefix( commandData.params[returnParams[1]].name, "p" ) );

          std::string const dataDeclarationTemplate =
            R"(std::vector<StructureChain${structureChainAllocator}> structureChains${structureChainInitializer};
    std::vector<${vectorElementType}> ${vectorVariable};
    ${counterType} ${counterVariable};)";

          return replaceWithMap( dataDeclarationTemplate,
                                 {
                                   { "counterType", dataTypes[0] },
                                   { "counterVariable", counterVariable },
                                   { "structureChainAllocator", structureChainAllocator },
                                   { "structureChainInitializer", structureChainInitializer },
                                   { "vectorElementType", dataTypes[1] },
                                   { "vectorVariable", vectorVariable },
                                 } );
        }
      }
      break;
    case 2:
      assert( ( returnParams[0] == std::next( vectorParams.begin() )->first ) && ( vectorParams.find( returnParams[1] ) == vectorParams.end() ) && !chained );
      {
        std::string firstDataVariable  = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );
        std::string secondDataVariable = startLowerCase( stripPrefix( commandData.params[returnParams[1]].name, "p" ) );
        if ( singular )
        {
          firstDataVariable = stripPluralS( firstDataVariable );

          std::string const dataDeclarationTemplate = R"(std::pair<${firstDataType},${secondDataType}> data;
    ${firstDataType} & ${firstDataVariable} = data.first;
    ${secondDataType} & ${secondDataVariable} = data.second;)";

          return replaceWithMap( dataDeclarationTemplate,
                                 { { "firstDataType", dataTypes[0] },
                                   { "firstDataVariable", firstDataVariable },
                                   { "secondDataType", dataTypes[1] },
                                   { "secondDataVariable", secondDataVariable } } );
        }
        else
        {
          std::string allocatorType       = raii ? "" : ( startUpperCase( stripPrefix( dataTypes[0], "VULKAN_HPP_NAMESPACE::" ) ) + "Allocator" );
          std::string allocateInitializer = withAllocator ? ( ", " + startLowerCase( allocatorType ) ) : "";
          if ( !raii )
          {
            allocatorType = ", " + allocatorType;
          }
          std::string vectorSize = startLowerCase( stripPrefix( commandData.params[vectorParams.begin()->first].name, "p" ) ) + ".size()";

          std::string const dataDeclarationTemplate =
            R"(std::pair<std::vector<${firstDataType}${allocatorType}>,${secondDataType}> data( std::piecewise_construct, std::forward_as_tuple( ${vectorSize}${allocateInitializer} ), std::forward_as_tuple( 0 ) );
    std::vector<${firstDataType}${allocatorType}> & ${firstDataVariable} = data.first;
    ${secondDataType} & ${secondDataVariable} = data.second;)";

          return replaceWithMap( dataDeclarationTemplate,
                                 { { "allocateInitializer", allocateInitializer },
                                   { "allocatorType", allocatorType },
                                   { "firstDataType", dataTypes[0] },
                                   { "firstDataVariable", firstDataVariable },
                                   { "secondDataType", dataTypes[1] },
                                   { "secondDataVariable", secondDataVariable },
                                   { "vectorSize", vectorSize } } );
        }
      }
      break;
    default: assert( false ); return "";
  }
}

std::string VulkanHppGenerator::generateDataDeclarations3Returns( CommandData const &                       commandData,
                                                                  std::vector<size_t> const &               returnParams,
                                                                  std::map<size_t, VectorParamData> const & vectorParams,
                                                                  CommandFlavourFlags                       flavourFlags,
                                                                  bool                                      raii,
                                                                  std::vector<std::string> const &          dataTypes ) const
{
  switch ( vectorParams.size() )
  {
    case 1:
      {
        assert( ( returnParams[1] == vectorParams.begin()->second.lenParam ) && ( returnParams[2] == vectorParams.begin()->first ) );

        std::string valueVariable   = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );
        std::string counterVariable = startLowerCase( stripPrefix( commandData.params[returnParams[1]].name, "p" ) );
        std::string vectorVariable  = startLowerCase( stripPrefix( commandData.params[returnParams[2]].name, "p" ) );
        std::string vectorAllocatorType, pairConstructor;
        if ( !raii )
        {
          vectorAllocatorType = startUpperCase( stripPrefix( dataTypes[2], "VULKAN_HPP_NAMESPACE::" ) ) + "Allocator";
          pairConstructor =
            ( flavourFlags & CommandFlavourFlagBits::withAllocator )
              ? ( "( std::piecewise_construct, std::forward_as_tuple( false ), std::forward_as_tuple( " + startLowerCase( vectorAllocatorType ) + " ) )" )
              : "";
          vectorAllocatorType = ", " + vectorAllocatorType;
        }

        std::string const dataDeclarationsTemplate =
          R"(std::pair<${valueType},std::vector<${vectorElementType}${vectorAllocatorType}>> data${pairConstructor};
    ${valueType} & ${valueVariable} = data.first;
    std::vector<${vectorElementType}${vectorAllocatorType}> & ${vectorVariable} = data.second;
    ${counterType} ${counterVariable};)";

        return replaceWithMap( dataDeclarationsTemplate,
                               { { "counterType", dataTypes[1] },
                                 { "counterVariable", counterVariable },
                                 { "vectorAllocatorType", vectorAllocatorType },
                                 { "vectorElementType", dataTypes[2] },
                                 { "vectorVariable", vectorVariable },
                                 { "pairConstructor", pairConstructor },
                                 { "valueType", dataTypes[0] },
                                 { "valueVariable", valueVariable } } );
      }
      break;
    case 2:
      {
        assert( ( returnParams[0] == vectorParams.begin()->second.lenParam ) && ( returnParams[1] == vectorParams.begin()->first ) &&
                ( returnParams[2] == std::next( vectorParams.begin() )->first ) && ( returnParams[0] == std::next( vectorParams.begin() )->second.lenParam ) );

        std::string counterVariable      = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );
        std::string firstVectorVariable  = startLowerCase( stripPrefix( commandData.params[returnParams[1]].name, "p" ) );
        std::string secondVectorVariable = startLowerCase( stripPrefix( commandData.params[returnParams[2]].name, "p" ) );
        std::string firstVectorAllocatorType, secondVectorAllocatorType, pairConstructor;
        if ( !raii )
        {
          firstVectorAllocatorType  = startUpperCase( stripPrefix( dataTypes[1], "VULKAN_HPP_NAMESPACE::" ) ) + "Allocator";
          secondVectorAllocatorType = startUpperCase( stripPrefix( dataTypes[2], "VULKAN_HPP_NAMESPACE::" ) ) + "Allocator";
          pairConstructor           = ( flavourFlags & CommandFlavourFlagBits::withAllocator )
                                      ? ( "( std::piecewise_construct, std::forward_as_tuple( " + startLowerCase( firstVectorAllocatorType ) +
                                " ), std::forward_as_tuple( " + startLowerCase( secondVectorAllocatorType ) + " ) )" )
                                      : "";
          firstVectorAllocatorType  = ", " + firstVectorAllocatorType;
          secondVectorAllocatorType = ", " + secondVectorAllocatorType;
        }

        std::string const dataDeclarationsTemplate =
          R"(std::pair<std::vector<${firstVectorElementType}${firstVectorAllocatorType}>, std::vector<${secondVectorElementType}${secondVectorAllocatorType}>> data${pairConstructor};
    std::vector<${firstVectorElementType}${firstVectorAllocatorType}> & ${firstVectorVariable} = data.first;
    std::vector<${secondVectorElementType}${secondVectorAllocatorType}> & ${secondVectorVariable} = data.second;
    ${counterType} ${counterVariable};)";

        return replaceWithMap( dataDeclarationsTemplate,
                               { { "counterType", dataTypes[0] },
                                 { "counterVariable", counterVariable },
                                 { "firstVectorAllocatorType", firstVectorAllocatorType },
                                 { "firstVectorElementType", dataTypes[1] },
                                 { "firstVectorVariable", firstVectorVariable },
                                 { "pairConstructor", pairConstructor },
                                 { "secondVectorAllocatorType", secondVectorAllocatorType },
                                 { "secondVectorElementType", dataTypes[2] },
                                 { "secondVectorVariable", secondVectorVariable } } );
      }
    default: assert( false ); return "";
  }
}

std::string VulkanHppGenerator::generateDataPreparation( CommandData const &                       commandData,
                                                         size_t                                    initialSkipCount,
                                                         std::vector<size_t> const &               returnParams,
                                                         std::map<size_t, VectorParamData> const & vectorParams,
                                                         std::set<size_t> const &                  templatedParams,
                                                         CommandFlavourFlags                       flavourFlags,
                                                         bool                                      enumerating ) const
{
  bool chained  = flavourFlags & CommandFlavourFlagBits::chained;
  bool singular = flavourFlags & CommandFlavourFlagBits::singular;
  bool unique   = flavourFlags & CommandFlavourFlagBits::unique;

  auto vectorParamIt = ( 1 < returnParams.size() ) ? vectorParams.find( returnParams[1] ) : vectorParams.end();
  if ( vectorParamIt != vectorParams.end() )
  {
    assert( !unique );

    std::string vectorName = startLowerCase( stripPrefix( commandData.params[vectorParamIt->first].name, "p" ) );

    if ( chained )
    {
      assert( !singular );
      assert( templatedParams.empty() );
      assert( returnParams.size() == 2 );
      assert( vectorParams.find( returnParams[0] ) == vectorParams.end() );
      assert( ( vectorParamIt != vectorParams.end() ) && ( vectorParamIt->second.lenParam == returnParams[0] ) );

      std::string vectorElementType = stripPostfix( commandData.params[vectorParamIt->first].type.compose( "VULKAN_HPP_NAMESPACE" ), " *" );

      if ( enumerating )
      {
        std::string const dataPreparationTemplate =
          R"(VULKAN_HPP_ASSERT( ${counterName} <= ${vectorName}.size() );
      if ( ${counterName} < ${vectorName}.size() )
      {
        structureChains.resize( ${counterName} );
      }
      for ( ${counterType} i = 0; i < ${counterName}; i++ )
      {
        structureChains[i].template get<${vectorElementType}>() = ${vectorName}[i];
      })";

        return replaceWithMap( dataPreparationTemplate,
                               { { "counterName", startLowerCase( stripPrefix( commandData.params[vectorParamIt->second.lenParam].name, "p" ) ) },
                                 { "counterType", commandData.params[vectorParamIt->second.lenParam].type.type },
                                 { "vectorElementType", vectorElementType },
                                 { "vectorName", vectorName } } );
      }
      else
      {
        std::string const dataPreparationTemplate =
          R"(for ( ${counterType} i = 0; i < ${counterName}; i++ )
    {
      structureChains[i].template get<${vectorElementType}>() = ${vectorName}[i];
    })";

        return replaceWithMap( dataPreparationTemplate,
                               { { "counterName", startLowerCase( stripPrefix( commandData.params[vectorParamIt->second.lenParam].name, "p" ) ) },
                                 { "counterType", commandData.params[vectorParamIt->second.lenParam].type.type },
                                 { "vectorElementType", vectorElementType },
                                 { "vectorName", vectorName } } );
      }
    }
    else if ( enumerating )
    {
      assert( !singular );
      assert( ( vectorParams.size() != 2 ) ||
              ( ( vectorParams.begin()->first == returnParams[1] ) && ( vectorParams.begin()->second.lenParam == returnParams[0] ) &&
                ( std::next( vectorParams.begin() )->first == returnParams[2] ) &&
                ( std::next( vectorParams.begin() )->second.lenParam == returnParams[0] ) ) );

      std::string resizes;
      for ( auto const & vp : vectorParams )
      {
        assert( ( std::find( returnParams.begin(), returnParams.end(), vp.first ) != returnParams.end() ) &&
                ( std::find( returnParams.begin(), returnParams.end(), vp.second.lenParam ) != returnParams.end() ) );
        resizes += startLowerCase( stripPrefix( commandData.params[vp.first].name, "p" ) ) + ".resize( " +
                   startLowerCase( stripPrefix( commandData.params[vp.second.lenParam].name, "p" ) ) + " );\n";
      }
      resizes.pop_back();

      std::string const dataPreparationTemplate =
        R"(VULKAN_HPP_ASSERT( ${counterName} <= ${vectorName}.size() );
    if ( ${counterName} < ${vectorName}.size() )
    {
      ${resizes}
    })";

      return replaceWithMap( dataPreparationTemplate,
                             { { "counterName", startLowerCase( stripPrefix( commandData.params[vectorParamIt->second.lenParam].name, "p" ) ) },
                               { "resizes", resizes },
                               { "vectorName", vectorName } } );
    }
  }
  else if ( unique && !singular && ( returnParams.size() == 1 ) && ( vectorParams.find( returnParams[0] ) != vectorParams.end() ) )
  {
    assert( !enumerating );
    std::string              className = initialSkipCount ? stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" ) : "";
    std::string              deleterDefinition;
    std::vector<std::string> lenParts = tokenize( commandData.params[returnParams[0]].lenExpression, "->" );
    switch ( lenParts.size() )
    {
      case 1: deleterDefinition = "ObjectDestroy<" + className + ", Dispatch> deleter( *this, allocator, d )"; break;
      case 2:
        {
          auto vpiIt = vectorParams.find( returnParams[0] );
          assert( vpiIt != vectorParams.end() );
          std::string poolType, poolName;
          std::tie( poolType, poolName ) = getPoolTypeAndName( commandData.params[vpiIt->second.lenParam].type.type );
          assert( !poolType.empty() );
          poolType          = stripPrefix( poolType, "Vk" );
          poolName          = startLowerCase( stripPrefix( lenParts[0], "p" ) ) + "." + poolName;
          deleterDefinition = "PoolFree<" + className + ", " + poolType + ", Dispatch> deleter( *this, " + poolName + ", d )";
        }
        break;
    }

    std::string handleType       = stripPrefix( commandData.params[returnParams[0]].type.type, "Vk" );
    std::string uniqueVectorName = "unique" + stripPrefix( commandData.params[returnParams[0]].name, "p" );
    std::string vectorAllocator  = ( flavourFlags & CommandFlavourFlagBits::withAllocator ) ? ( "( " + startLowerCase( handleType ) + "Allocator )" ) : "";
    std::string vectorName       = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );
    std::string elementName      = stripPluralS( vectorName );
    std::string vectorSize = getVectorSize( commandData.params, vectorParams, returnParams[0], commandData.params[returnParams[0]].type.type, templatedParams );

    std::string const dataPreparationTemplate =
      R"(std::vector<UniqueHandle<VULKAN_HPP_NAMESPACE::${handleType}, Dispatch>, ${handleType}Allocator> ${uniqueVectorName}${vectorAllocator};
    ${uniqueVectorName}.reserve( ${vectorSize} );
    ${deleterDefinition};
    for ( auto const & ${elementName} : ${vectorName} )
    {
      ${uniqueVectorName}.push_back( UniqueHandle<${handleType}, Dispatch>( ${elementName}, deleter ) );
    })";

    return replaceWithMap( dataPreparationTemplate,
                           { { "elementName", elementName },
                             { "deleterDefinition", deleterDefinition },
                             { "handleType", handleType },
                             { "uniqueVectorName", uniqueVectorName },
                             { "vectorAllocator", vectorAllocator },
                             { "vectorName", vectorName },
                             { "vectorSize", vectorSize } } );
  }
  return "";
}

std::string VulkanHppGenerator::generateDataSizeChecks( CommandData const &                       commandData,
                                                        std::vector<size_t> const &               returnParams,
                                                        std::vector<std::string> const &          returnParamTypes,
                                                        std::map<size_t, VectorParamData> const & vectorParams,
                                                        std::set<size_t> const &                  templatedParams,
                                                        bool                                      singular ) const
{
  assert( returnParams.size() == returnParamTypes.size() );
  std::string dataSizeChecks;
  if ( !singular )
  {
    const std::string dataSizeCheckTemplate = R"(    VULKAN_HPP_ASSERT( ${dataSize} % sizeof( ${dataType} ) == 0 );)";
    for ( size_t i = 0; i < returnParams.size(); i++ )
    {
      auto vectorParamIt = vectorParams.find( returnParams[i] );
      if ( ( vectorParamIt != vectorParams.end() ) && ( templatedParams.find( returnParams[i] ) != templatedParams.end() ) &&
           ( std::find( returnParams.begin(), returnParams.end(), vectorParamIt->second.lenParam ) == returnParams.end() ) )
      {
        dataSizeChecks += replaceWithMap( dataSizeCheckTemplate,
                                          { { "dataSize", commandData.params[vectorParamIt->second.lenParam].name }, { "dataType", returnParamTypes[i] } } );
      }
    }
  }

  return dataSizeChecks;
}

std::string VulkanHppGenerator::generateDispatchLoaderDynamic() const
{
  const std::string dispatchLoaderDynamicTemplate = R"(
  using PFN_dummy = void ( * )();

  class DispatchLoaderDynamic : public DispatchLoaderBase
  {
  public:
${commandMembers}

  public:
    DispatchLoaderDynamic() VULKAN_HPP_NOEXCEPT = default;
    DispatchLoaderDynamic( DispatchLoaderDynamic const & rhs ) VULKAN_HPP_NOEXCEPT = default;

    DispatchLoaderDynamic(PFN_vkGetInstanceProcAddr getInstanceProcAddr) VULKAN_HPP_NOEXCEPT
    {
      init(getInstanceProcAddr);
    }

    void init( PFN_vkGetInstanceProcAddr getInstanceProcAddr ) VULKAN_HPP_NOEXCEPT
    {
      VULKAN_HPP_ASSERT(getInstanceProcAddr);

      vkGetInstanceProcAddr = getInstanceProcAddr;

${initialCommandAssignments}
    }

    // This interface does not require a linked vulkan library.
    DispatchLoaderDynamic( VkInstance                instance,
                           PFN_vkGetInstanceProcAddr getInstanceProcAddr,
                           VkDevice                  device            = {},
                           PFN_vkGetDeviceProcAddr   getDeviceProcAddr = nullptr ) VULKAN_HPP_NOEXCEPT
    {
      init( instance, getInstanceProcAddr, device, getDeviceProcAddr );
    }

    // This interface does not require a linked vulkan library.
    void init( VkInstance                instance,
               PFN_vkGetInstanceProcAddr getInstanceProcAddr,
               VkDevice                  device              = {},
               PFN_vkGetDeviceProcAddr /*getDeviceProcAddr*/ = nullptr ) VULKAN_HPP_NOEXCEPT
    {
      VULKAN_HPP_ASSERT(instance && getInstanceProcAddr);
      vkGetInstanceProcAddr = getInstanceProcAddr;
      init( VULKAN_HPP_NAMESPACE::Instance(instance) );
      if (device) {
        init( VULKAN_HPP_NAMESPACE::Device(device) );
      }
    }

    void init( VULKAN_HPP_NAMESPACE::Instance instanceCpp ) VULKAN_HPP_NOEXCEPT
    {
      VkInstance instance = static_cast<VkInstance>(instanceCpp);

${instanceCommandAssignments}
    }

    void init( VULKAN_HPP_NAMESPACE::Device deviceCpp ) VULKAN_HPP_NOEXCEPT
    {
      VkDevice device = static_cast<VkDevice>(deviceCpp);

${deviceCommandAssignments}
    }

    template <typename DynamicLoader>
    void init(VULKAN_HPP_NAMESPACE::Instance const & instance, VULKAN_HPP_NAMESPACE::Device const & device, DynamicLoader const & dl) VULKAN_HPP_NOEXCEPT
    {
      PFN_vkGetInstanceProcAddr getInstanceProcAddr = dl.template getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
      PFN_vkGetDeviceProcAddr getDeviceProcAddr = dl.template getProcAddress<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
      init(static_cast<VkInstance>(instance), getInstanceProcAddr, static_cast<VkDevice>(device), device ? getDeviceProcAddr : nullptr);
    }

    template <typename DynamicLoader
#if VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
      = VULKAN_HPP_NAMESPACE::DynamicLoader
#endif
    >
    void init(VULKAN_HPP_NAMESPACE::Instance const & instance, VULKAN_HPP_NAMESPACE::Device const & device) VULKAN_HPP_NOEXCEPT
    {
      static DynamicLoader dl;
      init(instance, device, dl);
    }
  };)";

  std::string           commandMembers, deviceCommandAssignments, initialCommandAssignments, instanceCommandAssignments;
  std::set<std::string> listedCommands;  // some commands are listed with more than one extension!
  for ( auto const & feature : m_features )
  {
    appendDispatchLoaderDynamicCommands(
      feature.requireData, listedCommands, feature.name, commandMembers, initialCommandAssignments, instanceCommandAssignments, deviceCommandAssignments );
  }
  for ( auto const & extension : m_extensions )
  {
    appendDispatchLoaderDynamicCommands(
      extension.requireData, listedCommands, extension.name, commandMembers, initialCommandAssignments, instanceCommandAssignments, deviceCommandAssignments );
  }

  return replaceWithMap( dispatchLoaderDynamicTemplate,
                         { { "commandMembers", commandMembers },
                           { "deviceCommandAssignments", deviceCommandAssignments },
                           { "initialCommandAssignments", initialCommandAssignments },
                           { "instanceCommandAssignments", instanceCommandAssignments } } );
}

std::string VulkanHppGenerator::generateDispatchLoaderStatic() const
{
  const std::string dispatchLoaderStaticTemplate = R"(
#if !defined( VK_NO_PROTOTYPES )
  class DispatchLoaderStatic : public DispatchLoaderBase
  {
  public:
${commands}
  };
#endif
)";

  std::string           commands;
  std::set<std::string> listedCommands;
  for ( auto const & feature : m_features )
  {
    commands += generateDispatchLoaderStaticCommands( feature.requireData, listedCommands, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    commands += generateDispatchLoaderStaticCommands( extension.requireData, listedCommands, extension.name );
  }

  return replaceWithMap( dispatchLoaderStaticTemplate, { { "commands", commands } } );
}

std::string VulkanHppGenerator::generateDestroyCommand( std::string const & name, CommandData const & commandData ) const
{
  // special handling for destroy functions, filter out alias functions
  std::string commandName = generateCommandName( name, commandData.params, 1 );
  if ( ( m_commandAliases.find( name ) == m_commandAliases.end() ) &&
       ( ( ( name.substr( 2, 7 ) == "Destroy" ) && ( commandName != "destroy" ) ) || ( name.substr( 2, 4 ) == "Free" ) ||
         ( name == "vkReleasePerformanceConfigurationINTEL" ) ) )
  {
    assert( 1 < commandData.params.size() );
    // make sure, the object to destroy/free/release is not optional in the shortened version!
    CommandData localCommandData        = commandData;
    localCommandData.params[1].optional = false;

    std::string destroyCommandString = generateCommand( name, localCommandData, 1, false, false );
    std::string shortenedName;
    if ( name.substr( 2, 7 ) == "Destroy" )
    {
      shortenedName = "destroy";
    }
    else if ( name.substr( 2, 4 ) == "Free" )
    {
      // enclose "free" in parenthesis to prevent interference with MSVC debug free
      shortenedName = "( free )";
    }
    else
    {
      assert( name == "vkReleasePerformanceConfigurationINTEL" );
      shortenedName = "release";
    }
    size_t pos = destroyCommandString.find( commandName );
    while ( pos != std::string::npos )
    {
      destroyCommandString.replace( pos, commandName.length(), shortenedName );
      pos = destroyCommandString.find( commandName, pos );
    }
    // we need to remove the default argument for the first argument, to prevent ambiguities!
    assert( 1 < localCommandData.params.size() );
    pos = destroyCommandString.find( localCommandData.params[1].name );  // skip the standard version of the function
    assert( pos != std::string::npos );
    pos = destroyCommandString.find( localCommandData.params[1].name,
                                     pos + 1 );  // get the argument to destroy in the advanced version
    assert( pos != std::string::npos );
    pos = destroyCommandString.find( " VULKAN_HPP_DEFAULT_ARGUMENT_ASSIGNMENT", pos );
    if ( pos != std::string::npos )
    {
      destroyCommandString.erase( pos, strlen( " VULKAN_HPP_DEFAULT_ARGUMENT_ASSIGNMENT" ) );
    }
    return "\n" + destroyCommandString;
  }
  return "";
}

std::string VulkanHppGenerator::generateDispatchLoaderDynamicCommandAssignment( std::string const & commandName, std::string const & firstArg ) const
{
  if ( commandName == "vkGetInstanceProcAddr" )
  {
    // Don't overwite vkGetInstanceProcAddr with NULL.
    return "";
  }
  std::string str = "      " + commandName + " = PFN_" + commandName + "( vkGet" + ( ( firstArg == "device" ) ? "Device" : "Instance" ) + "ProcAddr( " +
                    firstArg + ", \"" + commandName + "\" ) );\n";
  // if this is an alias'ed function, use it as a fallback for the original one
  auto aliasIt = m_commandAliases.find( commandName );
  if ( aliasIt != m_commandAliases.end() )
  {
    str += "      if ( !" + aliasIt->second.name + " ) " + aliasIt->second.name + " = " + commandName + ";\n";
  }
  return str;
}

std::string VulkanHppGenerator::generateDispatchLoaderStaticCommands( std::vector<RequireData> const & requireData,
                                                                      std::set<std::string> &          listedCommands,
                                                                      std::string const &              title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & command : require.commands )
    {
      // some commands are listed for multiple extensions !
      if ( listedCommands.insert( command ).second )
      {
        auto const & commandData = getCommandData( command );

        str += "\n";
        std::string parameterList, parameters;
        assert( !commandData.params.empty() );
        for ( auto param : commandData.params )
        {
          parameterList += param.type.compose( "" ) + " " + param.name + generateCArraySizes( param.arraySizes ) + ", ";
          parameters += param.name + ", ";
        }
        assert( parameterList.ends_with( ", " ) && parameters.ends_with( ", " ) );
        parameterList.resize( parameterList.size() - 2 );
        parameters.resize( parameters.size() - 2 );

        const std::string commandTemplate = R"(
    ${returnType} ${commandName}( ${parameterList} ) const VULKAN_HPP_NOEXCEPT
    {
      return ::${commandName}( ${parameters} );
    }
)";

        str += replaceWithMap(
          commandTemplate,
          { { "commandName", command }, { "parameterList", parameterList }, { "parameters", parameters }, { "returnType", commandData.returnType } } );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateEnum( std::pair<std::string, EnumData> const & enumData, std::string const & surroundingProtect ) const
{
  std::string baseType, bitmask;
  if ( enumData.second.isBitmask )
  {
    auto bitmaskIt =
      std::find_if( m_bitmasks.begin(), m_bitmasks.end(), [&enumData]( auto const & bitmask ) { return bitmask.second.require == enumData.first; } );
    assert( bitmaskIt != m_bitmasks.end() );
    baseType = " : " + bitmaskIt->first;
    bitmask  = generateBitmask( bitmaskIt, surroundingProtect );
  }

  auto        aliasEnumIt = findAlias( enumData.first, m_enumAliases );
  std::string enumValues, previousEnter, previousLeave;
#if !defined( NDEBUG )
  std::map<std::string, std::string> valueToNameMap;
#else
  std::set<std::string> valueSet;
#endif
  for ( auto const & value : enumData.second.values )
  {
    std::string valueName = generateEnumValueName(
      value.alias.empty() || ( aliasEnumIt == m_enumAliases.end() ) ? enumData.first : aliasEnumIt->first, value.name, enumData.second.isBitmask );
#if !defined( NDEBUG )
    auto valueToNameIt = valueToNameMap.find( valueName );
    if ( valueToNameIt == valueToNameMap.end() )
#else
    if ( valueSet.find( valueName ) == valueSet.end() )
#endif
    {
      // if the value's protect differs from the surrounding protect, generate protection code
      std::string enter, leave;
      if ( !value.protect.empty() && ( value.protect != surroundingProtect ) )
      {
        tie( enter, leave ) = generateProtection( value.protect );
      }
      if ( previousEnter != enter )
      {
        enumValues += previousLeave + enter;
      }
      enumValues += "    " + valueName + " = " + value.name + ",\n";

#if !defined( NDEBUG )
      if ( value.alias.empty() )
      {
        valueToNameMap[valueName] = value.name;
      }
      else
      {
        // map the aliasName to the name of the base
        std::string baseName = value.name;
        auto        valueIt  = std::find_if(
          enumData.second.values.begin(), enumData.second.values.end(), [&baseName]( EnumValueData const & evd ) { return evd.name == baseName; } );
        assert( valueIt != enumData.second.values.end() );
        while ( !valueIt->alias.empty() )
        {
          baseName = valueIt->alias;
          valueIt  = std::find_if(
            enumData.second.values.begin(), enumData.second.values.end(), [&baseName]( EnumValueData const & evd ) { return evd.name == baseName; } );
          if ( valueIt == enumData.second.values.end() )
          {
            valueIt = std::find_if( enumData.second.unsupportedValues.begin(),
                                    enumData.second.unsupportedValues.end(),
                                    [&baseName]( EnumValueData const & evd ) { return evd.name == baseName; } );
            assert( valueIt != enumData.second.unsupportedValues.end() );
          }
        }
        valueToNameMap[valueName] = baseName;
      }
#else
      valueSet.insert( valueName );
#endif

      previousEnter = enter;
      previousLeave = leave;
    }
#if !defined( NDEBUG )
    else
    {
      // verify, that the identical value represents the identical name
      auto valueIt =
        std::find_if( enumData.second.values.begin(), enumData.second.values.end(), [&value]( EnumValueData const & evd ) { return evd.name == value.name; } );
      std::string baseName = valueIt->name;
      while ( !valueIt->alias.empty() )
      {
        baseName = valueIt->alias;
        valueIt  = std::find_if(
          enumData.second.values.begin(), enumData.second.values.end(), [&baseName]( EnumValueData const & evd ) { return evd.name == baseName; } );
      }
      assert( baseName == valueToNameIt->second );
    }
#endif
  }
  enumValues += previousLeave;

  if ( !enumValues.empty() )
  {
    size_t pos = enumValues.rfind( ',' );
    assert( pos != std::string::npos );
    enumValues.erase( pos, 1 );
    enumValues = "\n" + enumValues + "  ";
  }

  std::string enumUsing;
  if ( aliasEnumIt != m_enumAliases.end() )
  {
    enumUsing += "  using " + stripPrefix( aliasEnumIt->first, "Vk" ) + " = " + stripPrefix( enumData.first, "Vk" ) + ";\n";
  }

  const std::string enumTemplate = R"(  enum class ${enumName}${baseType}
  {${enumValues}};
${enumUsing}${bitmask})";

  return replaceWithMap( enumTemplate,
                         { { "baseType", baseType },
                           { "bitmask", bitmask },
                           { "enumName", stripPrefix( enumData.first, "Vk" ) },
                           { "enumUsing", enumUsing },
                           { "enumValues", enumValues } } );
}

std::string VulkanHppGenerator::generateEnums() const
{
  const std::string enumsTemplate = R"(
  //=============
  //=== ENUMs ===
  //=============

${enums}
)";

  std::string           enums;
  std::set<std::string> listedEnums;
  for ( auto const & feature : m_features )
  {
    enums += generateEnums( feature.requireData, listedEnums, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    enums += generateEnums( extension.requireData, listedEnums, extension.name );
  }

  return replaceWithMap( enumsTemplate, { { "enums", enums } } );
}

std::string
  VulkanHppGenerator::generateEnums( std::vector<RequireData> const & requireData, std::set<std::string> & listedEnums, std::string const & title ) const
{
  std::string surroundingProtect = getProtectFromTitle( title );
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto enumIt = m_enums.find( type );
      if ( ( enumIt != m_enums.end() ) && ( listedEnums.find( type ) == listedEnums.end() ) )
      {
        listedEnums.insert( type );
        str += "\n";
        str += generateEnum( *enumIt, surroundingProtect );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateEnumsToString() const
{
  // start with toHexString, which is used in all the to_string functions here!
  const std::string enumsToStringTemplate = R"(
  //=======================
  //=== ENUMs to_string ===
  //=======================

  VULKAN_HPP_INLINE std::string toHexString( uint32_t value )
  {
#if __cpp_lib_format
    return std::format( "{:x}", value );
#else
    std::stringstream stream;
    stream << std::hex << value;
    return stream.str();
#endif
  }

${enumsToString}
)";

  std::string           enumsToString;
  std::set<std::string> listedEnums;
  for ( auto const & feature : m_features )
  {
    enumsToString += generateEnumsToString( feature.requireData, listedEnums, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    enumsToString += generateEnumsToString( extension.requireData, listedEnums, extension.name );
  }

  return replaceWithMap( enumsToStringTemplate, { { "enumsToString", enumsToString } } );
}

std::string VulkanHppGenerator::generateEnumsToString( std::vector<RequireData> const & requireData,
                                                       std::set<std::string> &          listedEnums,
                                                       std::string const &              title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto enumIt = m_enums.find( type );
      if ( ( enumIt != m_enums.end() ) && ( listedEnums.find( type ) == listedEnums.end() ) )
      {
        listedEnums.insert( type );

        str += "\n";
        str += generateEnumToString( *enumIt );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateEnumInitializer( TypeInfo const &                   type,
                                                         std::vector<std::string> const &   arraySizes,
                                                         std::vector<EnumValueData> const & values,
                                                         bool                               bitmask ) const
{
  // enum arguments might need special initialization
  assert( type.prefix.empty() && !values.empty() );
  std::string valueName = generateEnumValueName( type.type, values.front().name, bitmask );
  std::string value     = generateNamespacedType( type.type ) + "::" + valueName;
  std::string str;
  if ( arraySizes.empty() )
  {
    str += value;
  }
  else
  {
    assert( arraySizes.size() == 1 );
    auto constIt = m_constants.find( arraySizes[0] );
    int  count   = std::stoi( ( constIt == m_constants.end() ) ? arraySizes[0] : constIt->second.value );
    assert( 1 < count );
    str += "{ { " + value;
    for ( int i = 1; i < count; i++ )
    {
      str += ", " + value;
    }
    str += " } }";
  }
  return str;
}

std::string VulkanHppGenerator::generateEnumToString( std::pair<std::string, EnumData> const & enumData ) const
{
  std::string enumName = stripPrefix( enumData.first, "Vk" );
  std::string functionBody;
  if ( enumData.second.values.empty() )
  {
    functionBody = R"x(    return "(void)";)x";
  }
  else
  {
    std::string cases, previousEnter, previousLeave;
    for ( auto const & value : enumData.second.values )
    {
      // generate cases for non-alias enum values only
      if ( value.alias.empty() )
      {
        auto [enter, leave] = generateProtection( value.protect );
        if ( previousEnter != enter )
        {
          cases += previousLeave + enter;
        }

        const std::string caseTemplate = R"(      case ${enumName}::e${valueName} : return "${valueName}";
)";
        cases += replaceWithMap(
          caseTemplate,
          { { "enumName", enumName }, { "valueName", generateEnumValueName( enumData.first, value.name, enumData.second.isBitmask ).substr( 1 ) } } );

        previousEnter = enter;
        previousLeave = leave;
      }
    }
    cases += previousLeave;

    const std::string functionBodyTemplate =
      R"x(    switch ( value )
    {
${cases}      default: return "invalid ( " + VULKAN_HPP_NAMESPACE::toHexString( static_cast<uint32_t>( value ) ) + " )";
    }
)x";

    functionBody = replaceWithMap( functionBodyTemplate, { { "cases", cases } } );
  }

  const std::string enumToStringTemplate = R"(
  VULKAN_HPP_INLINE std::string to_string( ${enumName}${argument} )
  {
${functionBody}
  }
)";

  return replaceWithMap( enumToStringTemplate,
                         { { "argument", enumData.second.values.empty() ? "" : " value" }, { "enumName", enumName }, { "functionBody", functionBody } } );
}

std::pair<std::string, std::string> VulkanHppGenerator::generateEnumSuffixes( std::string const & name, bool bitmask ) const
{
  std::string prefix, postfix;
  if ( name == "VkResult" )
  {
    prefix = "VK_";
  }
  else
  {
    if ( bitmask )
    {
      // for a bitmask enum, start with "VK", cut off the trailing "FlagBits", and convert that name to upper case
      // end that with "Bit"
      size_t pos = name.find( "FlagBits" );
      assert( pos != std::string::npos );
      std::string shortenedName = name;
      shortenedName.erase( pos, strlen( "FlagBits" ) );
      std::string tag = findTag( shortenedName );
      prefix          = toUpperCase( stripPostfix( shortenedName, tag ) ) + "_";
    }
    else
    {
      // for a non-bitmask enum, convert the name to upper case
      prefix = toUpperCase( name ) + "_";
    }

    // if the enum name contains a tag move it from the prefix to the postfix to generate correct enum value
    // names.
    for ( auto const & tag : m_tags )
    {
      if ( prefix.ends_with( tag.first + "_" ) )
      {
        prefix.erase( prefix.length() - tag.first.length() - 1 );
        postfix = "_" + tag.first;
        break;
      }
      else if ( name.ends_with( tag.first ) )
      {
        postfix = "_" + tag.first;
        break;
      }
    }
  }

  return std::make_pair( prefix, postfix );
}

std::string VulkanHppGenerator::generateEnumValueName( std::string const & enumName, std::string const & valueName, bool bitmask ) const
{
  std::string prefix, postfix;
  std::tie( prefix, postfix ) = generateEnumSuffixes( enumName, bitmask );
  std::string tag             = findTag( valueName, "" );
  if ( postfix == "_" + tag )
  {
    tag = findTag( valueName, postfix );
  }

  std::string result = "e" + toCamelCase( stripPostfix( stripPrefix( valueName, prefix ), postfix ) );
  if ( bitmask )
  {
    size_t pos = result.find( "Bit" );
    if ( pos != std::string::npos )
    {
      result.erase( pos, 3 );
    }
  }
  if ( !tag.empty() && ( result.substr( result.length() - tag.length() ) == toCamelCase( tag ) ) )
  {
    result = result.substr( 0, result.length() - tag.length() ) + tag;
  }
  return result;
}

std::string VulkanHppGenerator::generateExtensionDeprecatedBy() const
{
  std::string deprecatedBy, previousEnter, previousLeave;
  for ( auto const & extension : m_extensions )
  {
    if ( extension.isDeprecated )
    {
      auto [enter, leave] = generateProtection( getProtectFromTitle( extension.name ) );
      deprecatedBy += ( ( previousEnter != enter ) ? ( "\n" + previousLeave + enter ) : "\n" ) + "  if ( name == \"" + extension.name + "\" ) { return \"" +
                      extension.deprecatedBy + "\"; }";
      previousEnter = enter;
      previousLeave = leave;
    }
  }
  if ( !previousLeave.empty() )
  {
    deprecatedBy += "\n" + previousLeave;
  }
  deprecatedBy += "\n  return \"\";";
  return deprecatedBy;
}

std::string VulkanHppGenerator::generateExtensionDeprecatedTest() const
{
  std::string deprecatedTest, previousEnter, previousLeave;
  for ( auto const & extension : m_extensions )
  {
    if ( extension.isDeprecated )
    {
      auto [enter, leave] = generateProtection( getProtectFromTitle( extension.name ) );
      deprecatedTest += ( ( previousEnter != enter ) ? ( "\n" + previousLeave + enter ) : "\n" ) + "( name == \"" + extension.name + "\" ) || ";
      previousEnter = enter;
      previousLeave = leave;
    }
  }
  if ( m_api == "vulkan" )
  {
    assert( deprecatedTest.ends_with( " || " ) );
    deprecatedTest = deprecatedTest.substr( 0, deprecatedTest.length() - 4 );
  }
  if ( !previousLeave.empty() )
  {
    deprecatedTest += "\n" + previousLeave;
  }
  if ( m_api != "vulkan" )
  {
    deprecatedTest += "false";  // there might be no deprecations at all, so add a "false" at the end...
  }
  return deprecatedTest;
}

std::string VulkanHppGenerator::generateExtensionTypeTest( std::string const & type ) const
{
  std::string typeTest, previousEnter, previousLeave;
  for ( auto const & extension : m_extensions )
  {
    if ( extension.type == type )
    {
      auto [enter, leave] = generateProtection( getProtectFromTitle( extension.name ) );
      typeTest += ( ( previousEnter != enter ) ? ( "\n" + previousLeave + enter ) : "\n" ) + "( name == \"" + extension.name + "\" ) || ";
      previousEnter = enter;
      previousLeave = leave;
    }
  }
  assert( typeTest.ends_with( " || " ) );
  typeTest = typeTest.substr( 0, typeTest.length() - 4 );
  if ( !previousLeave.empty() )
  {
    typeTest += "\n" + previousLeave;
  }
  return typeTest;
}

std::string VulkanHppGenerator::generateFailureCheck( std::vector<std::string> const & successCodes ) const
{
  assert( !successCodes.empty() );
  std::string failureCheck = "result != " + generateSuccessCode( successCodes[0] );
  if ( 1 < successCodes.size() )
  {
    failureCheck = "( " + failureCheck + " )";
    for ( size_t i = 1; i < successCodes.size(); ++i )
    {
      failureCheck += "&& ( result != " + generateSuccessCode( successCodes[i] ) + " )";
    }
  }
  return failureCheck;
}

std::string VulkanHppGenerator::generateFormatTraits() const
{
  if ( m_formats.empty() )
  {
    return "";
  }

  const std::string formatTraitsTemplate = R"(
  //=====================
  //=== Format Traits ===
  //=====================

  // The three-dimensional extent of a texel block.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 std::array<uint8_t, 3> blockExtent( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${blockExtentCases}
      default: return {{1, 1, 1 }};
    }
  }

  // The texel block size in bytes.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t blockSize( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${blockSizeCases}
      default : VULKAN_HPP_ASSERT( false ); return 0;
    }
  }

  // The class of the format (can't be just named "class"!)
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 char const * compatibilityClass( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${classCases}
      default : VULKAN_HPP_ASSERT( false ); return "";
    }
  }

  // The number of bits in this component, if not compressed, otherwise 0.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t componentBits( VULKAN_HPP_NAMESPACE::Format format, uint8_t component )
  {
    switch( format )
    {
${componentBitsCases}
      default: return 0;
    }
  }

  // The number of components of this format.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t componentCount( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${componentCountCases}
      default: return 0;
    }
  }

  // The name of the component
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 char const * componentName( VULKAN_HPP_NAMESPACE::Format format, uint8_t component )
  {
    switch( format )
    {
${componentNameCases}
      default: return "";
    }
  }

  // The numeric format of the component
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 char const * componentNumericFormat( VULKAN_HPP_NAMESPACE::Format format, uint8_t component )
  {
    switch( format )
    {
${componentNumericFormatCases}
      default: return "";
    }
  }

  // The plane this component lies in.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t componentPlaneIndex( VULKAN_HPP_NAMESPACE::Format format, uint8_t component )
  {
    switch( format )
    {
${componentPlaneIndexCases}
      default: return 0;
    }
  }

  // True, if the components of this format are compressed, otherwise false.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 bool componentsAreCompressed( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${componentsAreCompressedCases}
        return true;
      default: return false;
    }
  }

  // A textual description of the compression scheme, or an empty string if it is not compressed
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 char const * compressionScheme( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${compressionSchemeCases}
      default: return "";
    }
  }

  // True, if this format is a compressed one.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 bool isCompressed( VULKAN_HPP_NAMESPACE::Format format )
  {
    return ( *VULKAN_HPP_NAMESPACE::compressionScheme( format ) != 0 );
  }

  // The number of bits into which the format is packed. A single image element in this format
  // can be stored in the same space as a scalar type of this bit width.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t packed( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${packedCases}
      default: return 0;
    }
  }

  // The single-plane format that this plane is compatible with.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 VULKAN_HPP_NAMESPACE::Format planeCompatibleFormat( VULKAN_HPP_NAMESPACE::Format format, uint8_t plane )
  {
    switch( format )
    {
${planeCompatibleCases}
      default: VULKAN_HPP_ASSERT( plane == 0 ); return format;
    }
  }

  // The number of image planes of this format.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t planeCount( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${planeCountCases}
      default: return 1;
    }
  }

  // The relative height of this plane. A value of k means that this plane is 1/k the height of the overall format.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t planeHeightDivisor( VULKAN_HPP_NAMESPACE::Format format, uint8_t plane )
  {
    switch( format )
    {
${planeHeightDivisorCases}
      default: VULKAN_HPP_ASSERT( plane == 0 ); return 1;
    }
  }

  // The relative width of this plane. A value of k means that this plane is 1/k the width of the overall format.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t planeWidthDivisor( VULKAN_HPP_NAMESPACE::Format format, uint8_t plane )
  {
    switch( format )
    {
${planeWidthDivisorCases}
      default: VULKAN_HPP_ASSERT( plane == 0 ); return 1;
    }
  }

  // The number of texels in a texel block.
  VULKAN_HPP_INLINE VULKAN_HPP_CONSTEXPR_14 uint8_t texelsPerBlock( VULKAN_HPP_NAMESPACE::Format format )
  {
    switch( format )
    {
${texelsPerBlockCases}
      default: VULKAN_HPP_ASSERT( false ); return 0;
    }
  }
)";

  auto formatIt = m_enums.find( "VkFormat" );
  assert( formatIt != m_enums.end() );
  assert( formatIt->second.values.front().name == "VK_FORMAT_UNDEFINED" );

  std::string blockSizeCases, blockExtentCases, classCases, componentBitsCases, componentCountCases, componentNameCases, componentNumericFormatCases,
    componentPlaneIndexCases, componentsAreCompressedCases, compressionSchemeCases, packedCases, planeCompatibleCases, planeCountCases, planeHeightDivisorCases,
    planeWidthDivisorCases, texelsPerBlockCases;
  for ( auto formatValuesIt = std::next( formatIt->second.values.begin() ); formatValuesIt != formatIt->second.values.end(); ++formatValuesIt )
  {
    // only generate a trait for non-alias formats
    if ( formatValuesIt->alias.empty() )
    {
      auto traitIt = m_formats.find( formatValuesIt->name );
      assert( traitIt != m_formats.end() );
      std::string caseString = "      case VULKAN_HPP_NAMESPACE::Format::" + generateEnumValueName( "VkFormat", traitIt->first, false ) + ":";

      blockSizeCases += caseString + " return " + traitIt->second.blockSize + ";\n";

      if ( !traitIt->second.blockExtent.empty() )
      {
        std::vector<std::string> blockExtent = tokenize( traitIt->second.blockExtent, "," );
        assert( blockExtent.size() == 3 );
        blockExtentCases += caseString + " return {{ " + blockExtent[0] + ", " + blockExtent[1] + ", " + blockExtent[2] + " }};\n";
      }

      classCases += caseString + " return \"" + traitIt->second.classAttribute + "\";\n";

      if ( traitIt->second.components.front().bits != "compressed" )
      {
        const std::string componentBitsCaseTemplate = R"(${caseString}
        switch( component )
        {
${componentCases}
          default: VULKAN_HPP_ASSERT( false ); return 0;
        }
)";

        std::string componentCases;
        for ( size_t i = 0; i < traitIt->second.components.size(); ++i )
        {
          componentCases += "          case " + std::to_string( i ) + ": return " + traitIt->second.components[i].bits + ";\n";
        }
        componentCases.pop_back();
        componentBitsCases += replaceWithMap( componentBitsCaseTemplate, { { "caseString", caseString }, { "componentCases", componentCases } } );
      }

      componentCountCases += caseString + " return " + std::to_string( traitIt->second.components.size() ) + ";\n";

      {
        const std::string componentNameCaseTemplate = R"(${caseString}
        switch( component )
        {
${componentCases}
          default: VULKAN_HPP_ASSERT( false ); return "";
        }
)";

        std::string componentCases;
        for ( size_t i = 0; i < traitIt->second.components.size(); ++i )
        {
          componentCases += "          case " + std::to_string( i ) + ": return \"" + traitIt->second.components[i].name + "\";\n";
        }
        componentCases.pop_back();
        componentNameCases += replaceWithMap( componentNameCaseTemplate, { { "caseString", caseString }, { "componentCases", componentCases } } );
      }

      {
        const std::string componentNumericFormatCaseTemplate = R"(${caseString}
        switch( component )
        {
${componentCases}
          default: VULKAN_HPP_ASSERT( false ); return "";
        }
)";

        std::string componentCases;
        for ( size_t i = 0; i < traitIt->second.components.size(); ++i )
        {
          componentCases += "          case " + std::to_string( i ) + ": return \"" + traitIt->second.components[i].numericFormat + "\";\n";
        }
        componentCases.pop_back();
        componentNumericFormatCases +=
          replaceWithMap( componentNumericFormatCaseTemplate, { { "caseString", caseString }, { "componentCases", componentCases } } );
      }

      if ( !traitIt->second.components.front().planeIndex.empty() )
      {
        const std::string componentPlaneIndexCaseTemplate = R"(${caseString}
        switch( component )
        {
${componentCases}
          default: VULKAN_HPP_ASSERT( false ); return 0;
        }
)";

        std::string componentCases;
        for ( size_t i = 0; i < traitIt->second.components.size(); ++i )
        {
          componentCases += "          case " + std::to_string( i ) + ": return " + traitIt->second.components[i].planeIndex + ";\n";
        }
        componentCases.pop_back();
        componentPlaneIndexCases += replaceWithMap( componentPlaneIndexCaseTemplate, { { "caseString", caseString }, { "componentCases", componentCases } } );
      }

      if ( traitIt->second.components.front().bits == "compressed" )
      {
        componentsAreCompressedCases += caseString + "\n";
      }

      if ( !traitIt->second.compressed.empty() )
      {
        compressionSchemeCases += caseString + " return \"" + traitIt->second.compressed + "\";\n";
      }

      if ( !traitIt->second.packed.empty() )
      {
        packedCases += caseString + " return " + traitIt->second.packed + ";\n";
      }

      if ( !traitIt->second.planes.empty() )
      {
        const std::string planeCompatibleCaseTemplate = R"(${caseString}
        switch( plane )
        {
${compatibleCases}
          default: VULKAN_HPP_ASSERT( false ); return VULKAN_HPP_NAMESPACE::Format::eUndefined;
        }
)";

        const std::string planeHeightDivisorCaseTemplate = R"(${caseString}
        switch( plane )
        {
${heightDivisorCases}
          default: VULKAN_HPP_ASSERT( false ); return 1;
        }
)";

        const std::string planeWidthDivisorCaseTemplate = R"(${caseString}
        switch( plane )
        {
${widthDivisorCases}
          default: VULKAN_HPP_ASSERT( false ); return 1;
        }
)";

        std::string compatibleCases, heightDivisorCases, widthDivisorCases;
        for ( size_t i = 0; i < traitIt->second.planes.size(); ++i )
        {
          compatibleCases += "          case " + std::to_string( i ) +
                             ": return VULKAN_HPP_NAMESPACE::Format::" + generateEnumValueName( "VkFormat", traitIt->second.planes[i].compatible, false ) +
                             ";\n";
          heightDivisorCases += "          case " + std::to_string( i ) + ": return " + traitIt->second.planes[i].heightDivisor + ";\n";
          widthDivisorCases += "          case " + std::to_string( i ) + ": return " + traitIt->second.planes[i].widthDivisor + ";\n";
        }
        compatibleCases.pop_back();
        heightDivisorCases.pop_back();
        widthDivisorCases.pop_back();

        planeCompatibleCases += replaceWithMap( planeCompatibleCaseTemplate, { { "caseString", caseString }, { "compatibleCases", compatibleCases } } );

        planeCountCases += caseString + " return " + std::to_string( traitIt->second.planes.size() ) + ";\n";

        planeHeightDivisorCases +=
          replaceWithMap( planeHeightDivisorCaseTemplate, { { "caseString", caseString }, { "heightDivisorCases", heightDivisorCases } } );

        planeWidthDivisorCases += replaceWithMap( planeWidthDivisorCaseTemplate, { { "caseString", caseString }, { "widthDivisorCases", widthDivisorCases } } );
      }

      texelsPerBlockCases += caseString + " return " + traitIt->second.texelsPerBlock + ";\n";
    }
  }

  return replaceWithMap( formatTraitsTemplate,
                         { { "blockExtentCases", blockExtentCases },
                           { "blockSizeCases", blockSizeCases },
                           { "classCases", classCases },
                           { "componentBitsCases", componentBitsCases },
                           { "componentCountCases", componentCountCases },
                           { "componentNameCases", componentNameCases },
                           { "componentNumericFormatCases", componentNumericFormatCases },
                           { "componentPlaneIndexCases", componentPlaneIndexCases },
                           { "componentsAreCompressedCases", componentsAreCompressedCases },
                           { "compressionSchemeCases", compressionSchemeCases },
                           { "packedCases", packedCases },
                           { "planeCompatibleCases", planeCompatibleCases },
                           { "planeCountCases", planeCountCases },
                           { "planeHeightDivisorCases", planeHeightDivisorCases },
                           { "planeWidthDivisorCases", planeWidthDivisorCases },
                           { "texelsPerBlockCases", texelsPerBlockCases } } );
}

std::string VulkanHppGenerator::generateFunctionPointerCheck( std::string const & function, std::set<std::string> const & requiredBy ) const
{
  std::string functionPointerCheck;
  if ( !requiredBy.empty() )
  {
    std::string message = "Function <" + function + "> requires <" + *requiredBy.begin() + ">";
    for ( auto it = std::next( requiredBy.begin() ); it != requiredBy.end(); ++it )
    {
      message += " or <" + *it + ">";
    }

    functionPointerCheck = "VULKAN_HPP_ASSERT( getDispatcher()->" + function + " && \"" + message + "\" );";
  }
  return functionPointerCheck;
}

std::string VulkanHppGenerator::generateHandle( std::pair<std::string, HandleData> const & handleData, std::set<std::string> & listedHandles ) const
{
  assert( listedHandles.find( handleData.first ) == listedHandles.end() );

  // first check for any handle that needs to be listed before this one
  std::string str = generateHandleDependencies( handleData, listedHandles );

  // list the commands of this handle
  if ( handleData.first.empty() )
  {
    // the free functions, not bound to any handle
    str += generateHandleEmpty( handleData.second );
  }
  else
  {
    // list all the commands that are mapped to members of this class
    std::string commands = generateHandleCommandDeclarations( handleData.second.commands );

    // create CPPType template specialization and the debugReportObjectType
    std::string valueName = handleData.second.objTypeEnum;
    valueName             = valueName.replace( 3, 0, "DEBUG_REPORT_" ) + "_EXT";
    auto enumIt           = m_enums.find( "VkDebugReportObjectTypeEXT" );
    assert( enumIt != m_enums.end() );
    auto valueIt =
      std::find_if( enumIt->second.values.begin(), enumIt->second.values.end(), [&valueName]( EnumValueData const & evd ) { return valueName == evd.name; } );

    std::string className = stripPrefix( handleData.first, "Vk" );
    std::string cppType, debugReportObjectType;
    if ( valueIt == enumIt->second.values.end() )
    {
      debugReportObjectType = "eUnknown";
    }
    else
    {
      static const std::string cppTypeFromDebugReportObjectTypeEXTTemplate = R"(
  template <>
  struct CppType<VULKAN_HPP_NAMESPACE::DebugReportObjectTypeEXT, VULKAN_HPP_NAMESPACE::DebugReportObjectTypeEXT::e${className}>
  {
    using Type = VULKAN_HPP_NAMESPACE::${className};
  };
)";
      cppType               = replaceWithMap( cppTypeFromDebugReportObjectTypeEXTTemplate, { { "className", className } } );
      debugReportObjectType = generateEnumValueName( enumIt->first, valueIt->name, false );
    }

    auto [enter, leave] = generateProtection( getProtectFromType( handleData.first ) );

    assert( !handleData.second.objTypeEnum.empty() );
    enumIt = m_enums.find( "VkObjectType" );
    assert( enumIt != m_enums.end() );
    valueIt = std::find_if( enumIt->second.values.begin(),
                            enumIt->second.values.end(),
                            [&handleData]( EnumValueData const & evd ) { return evd.name == handleData.second.objTypeEnum; } );
    assert( valueIt != enumIt->second.values.end() );

    std::string usingAlias;
    auto        aliasIt = findAlias( handleData.first, m_handleAliases );
    if ( aliasIt != m_handleAliases.end() )
    {
      usingAlias += "  using " + stripPrefix( aliasIt->first, "Vk" ) + " = " + stripPrefix( handleData.first, "Vk" ) + ";\n";
    }

    const std::string typesafeExplicitKeyword          = handleData.second.isDispatchable ? "" : "VULKAN_HPP_TYPESAFE_EXPLICIT ";
    const std::string typesafeConversionConditional    = handleData.second.isDispatchable ? "" : "#if defined(VULKAN_HPP_TYPESAFE_CONVERSION)\n";
    const std::string typesafeConversionConditionalEnd = handleData.second.isDispatchable ? "" : "#endif\n";

    static const std::string templateString = R"(
${enter}  class ${className}
  {
  public:
    using CType = Vk${className};
    using NativeType = Vk${className};

    static VULKAN_HPP_CONST_OR_CONSTEXPR VULKAN_HPP_NAMESPACE::ObjectType objectType = VULKAN_HPP_NAMESPACE::ObjectType::${objTypeEnum};
    static VULKAN_HPP_CONST_OR_CONSTEXPR VULKAN_HPP_NAMESPACE::DebugReportObjectTypeEXT debugReportObjectType = VULKAN_HPP_NAMESPACE::DebugReportObjectTypeEXT::${debugReportObjectType};

  public:
    VULKAN_HPP_CONSTEXPR ${className}() = default;
    VULKAN_HPP_CONSTEXPR ${className}( std::nullptr_t ) VULKAN_HPP_NOEXCEPT
    {}
    ${typesafeExplicitKeyword}${className}( Vk${className} ${memberName} ) VULKAN_HPP_NOEXCEPT
      : m_${memberName}( ${memberName} )
    {}

${typesafeConversionConditional}    ${className} & operator=(Vk${className} ${memberName}) VULKAN_HPP_NOEXCEPT
    {
      m_${memberName} = ${memberName};
      return *this;
    }
${typesafeConversionConditionalEnd}
    ${className} & operator=( std::nullptr_t ) VULKAN_HPP_NOEXCEPT
    {
      m_${memberName} = {};
      return *this;
    }

#if defined(VULKAN_HPP_HAS_SPACESHIP_OPERATOR)
    auto operator<=>( ${className} const & ) const = default;
#else
    bool operator==( ${className} const & rhs ) const VULKAN_HPP_NOEXCEPT
    {
      return m_${memberName} == rhs.m_${memberName};
    }

    bool operator!=(${className} const & rhs ) const VULKAN_HPP_NOEXCEPT
    {
      return m_${memberName} != rhs.m_${memberName};
    }

    bool operator<(${className} const & rhs ) const VULKAN_HPP_NOEXCEPT
    {
      return m_${memberName} < rhs.m_${memberName};
    }
#endif
${commands}
    ${typesafeExplicitKeyword}operator Vk${className}() const VULKAN_HPP_NOEXCEPT
    {
      return m_${memberName};
    }

    explicit operator bool() const VULKAN_HPP_NOEXCEPT
    {
      return m_${memberName} != VK_NULL_HANDLE;
    }

    bool operator!() const VULKAN_HPP_NOEXCEPT
    {
      return m_${memberName} == VK_NULL_HANDLE;
    }

  private:
    Vk${className} m_${memberName} = {};
  };

  template <>
  struct CppType<VULKAN_HPP_NAMESPACE::ObjectType, VULKAN_HPP_NAMESPACE::ObjectType::${objTypeEnum}>
  {
    using Type = VULKAN_HPP_NAMESPACE::${className};
  };

${CppType}

  template <>
  struct isVulkanHandleType<VULKAN_HPP_NAMESPACE::${className}>
  {
    static VULKAN_HPP_CONST_OR_CONSTEXPR bool value = true;
  };
${usingAlias}${leave})";

    str += replaceWithMap( templateString,
                           { { "className", className },
                             { "commands", commands },
                             { "CppType", cppType },
                             { "debugReportObjectType", debugReportObjectType },
                             { "enter", enter },
                             { "leave", leave },
                             { "memberName", startLowerCase( stripPrefix( handleData.first, "Vk" ) ) },
                             { "objTypeEnum", generateEnumValueName( enumIt->first, valueIt->name, false ) },
                             { "usingAlias", usingAlias },
                             { "typesafeExplicitKeyword", typesafeExplicitKeyword },
                             { "typesafeConversionConditional", typesafeConversionConditional },
                             { "typesafeConversionConditionalEnd", typesafeConversionConditionalEnd } } );
  }

  listedHandles.insert( handleData.first );
  return str;
}

std::string VulkanHppGenerator::generateHandleCommandDeclarations( std::set<std::string> const & commands ) const
{
  std::string           str;
  std::set<std::string> listedCommands;  // some commands are listed with more than one extension!
  for ( auto const & feature : m_features )
  {
    std::vector<std::string> commandNames = selectCommandsByHandle( feature.requireData, commands, listedCommands );
    if ( !commandNames.empty() )
    {
      str += "\n  //=== " + feature.name + " ===\n";
      for ( auto const & command : commandNames )
      {
        auto commandIt = m_commands.find( command );
        assert( commandIt != m_commands.end() );

        str += "\n";
        str += generateCommand( commandIt->first, commandIt->second, 1, false, false );
        str += generateDestroyCommand( commandIt->first, commandIt->second );
      }
    }
  }
  for ( auto const & extension : m_extensions )
  {
    std::vector<std::string> commandNames = selectCommandsByHandle( extension.requireData, commands, listedCommands );
    if ( !commandNames.empty() )
    {
      auto [enter, leave] = generateProtection( getProtectFromTitle( extension.name ) );
      str += "\n" + enter + "  //=== " + extension.name + " ===\n";
      for ( auto const & command : commandNames )
      {
        auto const & commandData = getCommandData( command );

        std::string commandString;
        std::string commandName = generateCommandName( command, commandData.params, 1 );
        str += "\n";
        str += generateCommand( command, commandData, 1, false, false );
        str += generateDestroyCommand( command, commandData );
      }
      str += leave;
    }
  }
  return str;
}

std::string VulkanHppGenerator::generateHandleDependencies( std::pair<std::string, HandleData> const & handleData, std::set<std::string> & listedHandles ) const
{
  std::string str;
  for ( auto const & command : handleData.second.commands )
  {
    auto const & commandData = getCommandData( command );
    for ( auto const & parameter : commandData.params )
    {
      auto handleIt = m_handles.find( parameter.type.type );
      if ( ( handleIt != m_handles.end() ) && ( parameter.type.type != handleData.first ) &&
           ( listedHandles.find( parameter.type.type ) == listedHandles.end() ) )
      {
        str += generateHandle( *handleIt, listedHandles );
      }
    }
  }
  return str;
}

std::string VulkanHppGenerator::generateHandleEmpty( HandleData const & handleData ) const
{
  std::string           str;
  std::set<std::string> listedCommands;  // some commands are listed with more than one extension !
  for ( auto const & feature : m_features )
  {
    std::vector<std::string> commands = selectCommandsByHandle( feature.requireData, handleData.commands, listedCommands );
    if ( !commands.empty() )
    {
      str += "\n  //=== " + feature.name + " ===\n";
      for ( auto const & command : commands )
      {
        auto commandIt = m_commands.find( command );
        assert( commandIt != m_commands.end() );
        str += "\n" + generateCommand( commandIt->first, commandIt->second, 0, false, false );
      }
    }
  }
#if !defined( NDEBUG )
  for ( auto const & extension : m_extensions )
  {
    assert( selectCommandsByHandle( extension.requireData, handleData.commands, listedCommands ).empty() );
  }
#endif
  return str;
}

std::string VulkanHppGenerator::generateHandleForwardDeclarations() const
{
  const std::string fowardDeclarationsTemplate = R"(
  //===================================
  //=== HANDLE forward declarations ===
  //===================================

${forwardDeclarations}
)";

  std::string forwardDeclarations;
  for ( auto const & feature : m_features )
  {
    forwardDeclarations += generateHandleForwardDeclarations( feature.requireData, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    forwardDeclarations += generateHandleForwardDeclarations( extension.requireData, extension.name );
  }

  return replaceWithMap( fowardDeclarationsTemplate, { { "forwardDeclarations", forwardDeclarations } } );
}

std::string VulkanHppGenerator::generateHandleForwardDeclarations( std::vector<RequireData> const & requireData, std::string const & title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto handleIt = m_handles.find( type );
      if ( handleIt != m_handles.end() )
      {
        str += "class " + stripPrefix( handleIt->first, "Vk" ) + ";\n";
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateHandleHashStructures( std::vector<RequireData> const & requireData, std::string const & title ) const
{
  const std::string hashTemplate = R"(
  template <> struct hash<VULKAN_HPP_NAMESPACE::${type}>
  {
    std::size_t operator()(VULKAN_HPP_NAMESPACE::${type} const & ${name}) const VULKAN_HPP_NOEXCEPT
    {
      return std::hash<Vk${type}>{}(static_cast<Vk${type}>(${name}));
    }
  };
)";

  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto handleIt = m_handles.find( type );
      if ( handleIt != m_handles.end() )
      {
        std::string handleType = stripPrefix( handleIt->first, "Vk" );
        std::string handleName = startLowerCase( handleType );
        str += replaceWithMap( hashTemplate, { { "name", handleName }, { "type", handleType } } );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateHandleHashStructures() const
{
  const std::string hashesTemplate = R"(
  //===================================
  //=== HASH structures for handles ===
  //===================================

${hashes}
)";

  std::string hashes;
  for ( auto const & feature : m_features )
  {
    hashes += generateHandleHashStructures( feature.requireData, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    hashes += generateHandleHashStructures( extension.requireData, extension.name );
  }
  return replaceWithMap( hashesTemplate, { { "hashes", hashes } } );
}

std::string VulkanHppGenerator::generateHandles() const
{
  // Note: reordering structs or handles by features and extensions is not possible!
  std::string str = R"(
  //===============
  //=== HANDLEs ===
  //===============

  template <typename Type>
  struct isVulkanHandleType
  {
    static VULKAN_HPP_CONST_OR_CONSTEXPR bool value = false;
  };
)";

  std::set<std::string> listedHandles;
  str += generateHandle( *m_handles.begin(), listedHandles );  // artificial handle without a name!
  for ( auto handleIt = std::next( m_handles.begin() ); handleIt != m_handles.end(); ++handleIt )
  {
    if ( ( listedHandles.find( handleIt->first ) == listedHandles.end() ) && isTypeUsed( handleIt->first ) )
    {
      str += generateHandle( *handleIt, listedHandles );
    }
  }
  return str;
}

std::string VulkanHppGenerator::generateIndexTypeTraits() const
{
  const std::string indexTypeTraitsTemplate = R"(
  //=========================
  //=== Index Type Traits ===
  //=========================

  template<typename T>
  struct IndexTypeValue
  {};

${indexTypeTraits}
)";

  auto indexType = m_enums.find( "VkIndexType" );
  assert( indexType != m_enums.end() );

  std::string           indexTypeTraits;
  std::set<std::string> listedCppTypes;
  for ( auto const & value : indexType->second.values )
  {
    std::string valueName = generateEnumValueName( indexType->first, value.name, false );
    std::string cppType;
    if ( !valueName.starts_with( "eNone" ) )
    {
      // get the bit count out of the value Name (8, 16, 32, ... ) and generate the cppType (uint8_t,...)
      assert( valueName.starts_with( "eUint" ) );
      auto beginDigit = valueName.begin() + strlen( "eUint" );
      assert( isdigit( *beginDigit ) );
      auto endDigit = std::find_if_not( beginDigit, valueName.end(), []( std::string::value_type c ) { return isdigit( c ); } );
      cppType       = "uint" + valueName.substr( strlen( "eUint" ), endDigit - beginDigit ) + "_t";
    }

    if ( !cppType.empty() )
    {
      if ( listedCppTypes.insert( cppType ).second )
      {
        // IndexType traits aren't necessarily invertible.
        // The Type -> Enum translation will only occur for the first prefixed enum value.
        // A hypothetical extension to this enum with a conflicting prefix will use the core spec value.
        const std::string typeToEnumTemplate = R"(
  template <>
  struct IndexTypeValue<${cppType}>
  {
    static VULKAN_HPP_CONST_OR_CONSTEXPR IndexType value = IndexType::${valueName};
  };
)";
        indexTypeTraits += replaceWithMap( typeToEnumTemplate, { { "cppType", cppType }, { "valueName", valueName } } );
      }

      // Enum -> Type translations are always able to occur.
      const std::string enumToTypeTemplate = R"(
  template <>
  struct CppType<IndexType, IndexType::${valueName}>
  {
    using Type = ${cppType};
  };
)";
      indexTypeTraits += replaceWithMap( enumToTypeTemplate, { { "cppType", cppType }, { "valueName", valueName } } );
    }
  }

  return replaceWithMap( indexTypeTraitsTemplate, { { "indexTypeTraits", indexTypeTraits } } );
}

std::string VulkanHppGenerator::generateLenInitializer(
  std::vector<MemberData>::const_iterator                                                                                 mit,
  std::map<std::vector<MemberData>::const_iterator, std::vector<std::vector<MemberData>::const_iterator>>::const_iterator litit,
  bool                                                                                                                    mutualExclusiveLens ) const
{
  std::string initializer;
  if ( mutualExclusiveLens )
  {
    // there are multiple mutually exclusive arrays related to this len
    for ( size_t i = 0; i + 1 < litit->second.size(); i++ )
    {
      auto        arrayIt      = litit->second[i];
      std::string argumentName = startLowerCase( stripPrefix( arrayIt->name, "p" ) ) + "_";
      initializer += "!" + argumentName + ".empty() ? " + argumentName + ".size() : ";
    }
    auto        arrayIt      = litit->second.back();
    std::string argumentName = startLowerCase( stripPrefix( arrayIt->name, "p" ) ) + "_";
    initializer += argumentName + ".size()";
  }
  else
  {
    auto arrayIt = litit->second.front();
    assert( ( arrayIt->lenExpressions.front() == litit->first->name ) ||
            ( ( arrayIt->lenExpressions.front() == "codeSize / 4" ) && ( litit->first->name == "codeSize" ) ) );

    assert( arrayIt->name.starts_with( "p" ) );
    std::string argumentName = startLowerCase( stripPrefix( arrayIt->name, "p" ) ) + "_";

    assert( mit->type.prefix.empty() && mit->type.postfix.empty() );
    initializer = argumentName + ".size()";
    if ( arrayIt->lenExpressions.front() == "codeSize / 4" )
    {
      initializer += " * 4";
    }
    if ( ( arrayIt->type.type == "void" ) &&
         ( std::count_if( arrayIt->type.postfix.begin(), arrayIt->type.postfix.end(), []( char c ) { return c == '*'; } ) < 2 ) )
    {
      initializer += " * sizeof(T)";
    }
  }
  if ( mit->type.type != "size_t" )
  {
    initializer = "static_cast<" + mit->type.type + ">( " + initializer + " )";
  }
  return initializer;
}

std::string VulkanHppGenerator::generateName( TypeInfo const & typeInfo ) const
{
  std::string name = stripPrefix( typeInfo.type, "Vk" );
  assert( typeInfo.prefix.find( '*' ) == std::string::npos );
  if ( typeInfo.postfix.find( '*' ) != std::string::npos )
  {
    assert( typeInfo.postfix.find_first_of( '*' ) == typeInfo.postfix.find_last_of( '*' ) );
    name = "p" + name;
  }
  else
  {
    name = startLowerCase( name );
  }
  return name;
}

std::string VulkanHppGenerator::generateNoExcept( std::vector<std::string> const &          errorCodes,
                                                  std::vector<size_t> const &               returnParams,
                                                  std::map<size_t, VectorParamData> const & vectorParams,
                                                  CommandFlavourFlags                       flavourFlags,
                                                  bool                                      vectorSizeCheck,
                                                  bool                                      raii ) const
{
  // noexcept is only possible with no error codes, and the return param (if any) is not a vector param (unless it's the singular version)
  return ( errorCodes.empty() &&
           ( ( flavourFlags & CommandFlavourFlagBits::singular ) || returnParams.empty() ||
             ( std::find_if( returnParams.begin(),
                             returnParams.end(),
                             [&vectorParams]( size_t rp ) { return vectorParams.find( rp ) != vectorParams.end(); } ) == returnParams.end() ) ) )
         ? ( vectorSizeCheck ? ( raii ? "" : " VULKAN_HPP_NOEXCEPT_WHEN_NO_EXCEPTIONS" ) : " VULKAN_HPP_NOEXCEPT" )
         : "";
}

std::string VulkanHppGenerator::generateObjectDeleter( std::string const & commandName,
                                                       CommandData const & commandData,
                                                       size_t              initialSkipCount,
                                                       size_t              returnParam ) const
{
  std::string objectDeleter, allocator;
  if ( ( commandName.find( "Acquire" ) != std::string::npos ) || ( commandName.find( "Get" ) != std::string::npos ) )
  {
    if ( ( commandName == "vkAcquirePerformanceConfigurationINTEL" ) || ( commandName == "vkGetRandROutputDisplayEXT" ) ||
         ( commandName == "vkGetWinrtDisplayNV" ) || ( commandName == "vkGetDrmDisplayEXT" ) )
    {
      objectDeleter = "ObjectRelease";
    }
    else
    {
      throw std::runtime_error( "Found " + commandName + " which requires special handling for the object deleter" );
    }
  }
  else if ( commandName.find( "Allocate" ) != std::string::npos )
  {
    objectDeleter = "ObjectFree";
    allocator     = "allocator, ";
  }
  else
  {
    assert( ( commandName.find( "Create" ) != std::string::npos ) || ( commandName.find( "Register" ) != std::string::npos ) );
    objectDeleter = "ObjectDestroy";
    allocator     = "allocator, ";
  }
  std::string className  = initialSkipCount ? stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" ) : "";
  std::string parentName = ( className.empty() || ( commandData.params[returnParam].type.type == "VkDevice" ) ) ? "NoParent" : className;
  return objectDeleter + "<" + parentName + ", Dispatch>( " + ( ( parentName == "NoParent" ) ? "" : "*this, " ) + allocator + "d )";
}

std::pair<std::string, std::string> VulkanHppGenerator::generateProtection( std::string const & protect ) const
{
  return protect.empty() ? std::make_pair( "", "" ) : std::make_pair( "#if defined( " + protect + " )\n", "#endif /*" + protect + "*/\n" );
}

std::string VulkanHppGenerator::generateRAIICommandDefinitions() const
{
  const std::string commandDefinitionsTemplate = R"(
  //===========================
  //=== COMMAND Definitions ===
  //===========================

${commandDefinitions}
)";

  std::string           commandDefinitions;
  std::set<std::string> listedCommands;  // some commands are listed with more than one extension!
  for ( auto const & feature : m_features )
  {
    commandDefinitions += generateRAIICommandDefinitions( feature.requireData, listedCommands, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    commandDefinitions += generateRAIICommandDefinitions( extension.requireData, listedCommands, extension.name );
  }

  return replaceWithMap( commandDefinitionsTemplate, { { "commandDefinitions", commandDefinitions } } );
}

std::string VulkanHppGenerator::generateRAIICommandDefinitions( std::vector<RequireData> const & requireData,
                                                                std::set<std::string> &          listedCommands,
                                                                std::string const &              title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & command : require.commands )
    {
      if ( listedCommands.insert( command ).second )
      {
        str += generateRAIIHandleCommand( command, determineInitialSkipCount( command ), true );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateRAIIDispatchers() const
{
  std::string contextInitializers, contextMembers, deviceAssignments, deviceMembers, instanceAssignments, instanceMembers;

  std::set<std::string> listedCommands;
  for ( auto const & feature : m_features )
  {
    appendRAIIDispatcherCommands( feature.requireData,
                                  listedCommands,
                                  feature.name,
                                  contextInitializers,
                                  contextMembers,
                                  deviceAssignments,
                                  deviceMembers,
                                  instanceAssignments,
                                  instanceMembers );
  }
  for ( auto const & extension : m_extensions )
  {
    appendRAIIDispatcherCommands( extension.requireData,
                                  listedCommands,
                                  extension.name,
                                  contextInitializers,
                                  contextMembers,
                                  deviceAssignments,
                                  deviceMembers,
                                  instanceAssignments,
                                  instanceMembers );
  }

  std::string contextDispatcherTemplate = R"(
    class ContextDispatcher : public DispatchLoaderBase
    {
    public:
      ContextDispatcher( PFN_vkGetInstanceProcAddr getProcAddr )
        : vkGetInstanceProcAddr( getProcAddr )${contextInitializers}
      {}

    public:
      PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = 0;
${contextMembers}
    };
)";

  std::string str = replaceWithMap( contextDispatcherTemplate, { { "contextInitializers", contextInitializers }, { "contextMembers", contextMembers } } );

  std::string instanceDispatcherTemplate = R"(
    class InstanceDispatcher : public DispatchLoaderBase
    {
    public:
      InstanceDispatcher( PFN_vkGetInstanceProcAddr getProcAddr, VkInstance instance )
        : vkGetInstanceProcAddr( getProcAddr )
      {
${instanceAssignments}
        vkGetDeviceProcAddr =
          PFN_vkGetDeviceProcAddr( vkGetInstanceProcAddr( instance, "vkGetDeviceProcAddr" ) );
      }

    public:
${instanceMembers}
      PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = 0;
    };
)";

  str += replaceWithMap( instanceDispatcherTemplate, { { "instanceAssignments", instanceAssignments }, { "instanceMembers", instanceMembers } } );

  std::string deviceDispatcherTemplate = R"(
    class DeviceDispatcher : public DispatchLoaderBase
    {
    public:
      DeviceDispatcher( PFN_vkGetDeviceProcAddr getProcAddr, VkDevice device ) : vkGetDeviceProcAddr( getProcAddr )
      {
${deviceAssignments}
      }

    public:
${deviceMembers}
    };
)";

  str += replaceWithMap( deviceDispatcherTemplate, { { "deviceAssignments", deviceAssignments }, { "deviceMembers", deviceMembers } } );
  return str;
}

std::string VulkanHppGenerator::generateRAIIHandle( std::pair<std::string, HandleData> const & handle,
                                                    std::set<std::string> &                    listedHandles,
                                                    std::set<std::string> const &              specialFunctions ) const
{
  std::string str;
  if ( listedHandles.find( handle.first ) == listedHandles.end() )
  {
    rescheduleRAIIHandle( str, handle, listedHandles, specialFunctions );

    auto [enter, leave]    = generateProtection( getProtectFromType( handle.first ) );
    std::string handleType = stripPrefix( handle.first, "Vk" );
    std::string handleName = generateRAIIHandleConstructorParamName( handle.first, handle.second.destructorIt );

    auto [singularConstructors, arrayConstructors] = generateRAIIHandleConstructors( handle );

    auto [clearMembers, getConstructorSuccessCode, memberVariables, moveConstructorInitializerList, moveAssignmentInstructions, swapMembers, releaseMembers] =
      generateRAIIHandleDetails( handle );

    std::string declarations = generateRAIIHandleCommandDeclarations( handle, specialFunctions );

    assert( !handle.second.objTypeEnum.empty() );
    auto enumIt = m_enums.find( "VkObjectType" );
    assert( enumIt != m_enums.end() );
    auto valueIt = std::find_if(
      enumIt->second.values.begin(), enumIt->second.values.end(), [&handle]( EnumValueData const & evd ) { return evd.name == handle.second.objTypeEnum; } );
    assert( valueIt != enumIt->second.values.end() );
    std::string objTypeEnum = generateEnumValueName( enumIt->first, valueIt->name, false );

    enumIt = m_enums.find( "VkDebugReportObjectTypeEXT" );
    assert( enumIt != m_enums.end() );
    std::string valueName = handle.second.objTypeEnum;
    valueName             = valueName.replace( 3, 0, "DEBUG_REPORT_" ) + "_EXT";
    valueIt =
      std::find_if( enumIt->second.values.begin(), enumIt->second.values.end(), [&valueName]( EnumValueData const & evd ) { return valueName == evd.name; } );
    std::string debugReportObjectType = ( valueIt != enumIt->second.values.end() ) ? generateEnumValueName( enumIt->first, valueIt->name, false ) : "eUnknown";

    std::string dispatcherType = ( ( handle.first == "VkDevice" ) || ( handle.second.constructorIts.front()->second.params.front().type.type == "VkDevice" ) )
                                 ? "VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::DeviceDispatcher"
                                 : "VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::InstanceDispatcher";

    std::string getParent;
    if ( ( handle.first != "VkInstance" ) && ( handle.first != "VkDevice" ) && ( handle.second.destructorIt != m_commands.end() ) )
    {
      assert( !handle.second.destructorIt->second.params.empty() );
      std::string parentType = stripPrefix( handle.second.destructorIt->second.params.front().type.type, "Vk" );
      getParent              = "    VULKAN_HPP_NAMESPACE::" + parentType + " get" + parentType + "() const\n";
      getParent += "    {\n";
      getParent += "      return m_" + handle.second.destructorIt->second.params.front().name + ";\n";
      getParent += "    }\n";
    }

    std::string assignmentOperator, copyConstructor;
    if ( handle.second.destructorIt == m_commands.end() )
    {
      // allow copy constructor and assignment operator for classes without destructor
      std::string const copyConstructorTemplate =
        R"(      ${handleType}( ${handleType} const & rhs ) : m_${handleName}( rhs.m_${handleName} ), m_dispatcher( rhs.m_dispatcher ) {})";
      copyConstructor += replaceWithMap( copyConstructorTemplate, { { "handleName", handleName }, { "handleType", handleType } } );

      std::string assignmentOperatorTemplate = R"(      ${handleType} & operator=( ${handleType} const & rhs )
      {
        m_${handleName} = rhs.m_${handleName};
        m_dispatcher    = rhs.m_dispatcher;
        return *this;
      })";
      assignmentOperator += replaceWithMap( assignmentOperatorTemplate, { { "handleName", handleName }, { "handleType", handleType } } );
    }
    else
    {
      std::string const copyConstructorTemplate = R"(      ${handleType}( ${handleType} const & ) = delete;)";
      copyConstructor += replaceWithMap( copyConstructorTemplate, { { "handleType", handleType } } );

      std::string const assignmentOperatorTemplate = R"(      ${handleType} & operator=( ${handleType} const & ) = delete;)";
      assignmentOperator += replaceWithMap( assignmentOperatorTemplate, { { "handleType", handleType } } );
    }

    const std::string handleTemplate = R"(
${enter}  class ${handleType}
  {
  public:
    using CType = Vk${handleType};

    static VULKAN_HPP_CONST_OR_CONSTEXPR VULKAN_HPP_NAMESPACE::ObjectType objectType = VULKAN_HPP_NAMESPACE::ObjectType::${objTypeEnum};
    static VULKAN_HPP_CONST_OR_CONSTEXPR VULKAN_HPP_NAMESPACE::DebugReportObjectTypeEXT debugReportObjectType = VULKAN_HPP_NAMESPACE::DebugReportObjectTypeEXT::${debugReportObjectType};

  public:
${singularConstructors}
    ${handleType}( std::nullptr_t ) {}

    ~${handleType}()
    {
      clear();
    }

    ${handleType}() = delete;
${copyConstructor}
    ${handleType}( ${handleType} && rhs ) VULKAN_HPP_NOEXCEPT
      : ${moveConstructorInitializerList}
    {}
${assignmentOperator}
    ${handleType} & operator=( ${handleType} && rhs ) VULKAN_HPP_NOEXCEPT
    {
      if ( this != &rhs )
      {
${moveAssignmentInstructions}
      }
      return *this;
    }

    VULKAN_HPP_NAMESPACE::${handleType} const & operator*() const VULKAN_HPP_NOEXCEPT
    {
      return m_${handleName};
    }

    void clear() VULKAN_HPP_NOEXCEPT
    {
${clearMembers}
    }

    VULKAN_HPP_NAMESPACE::${handleType} release()
    {
${releaseMembers}
    }

${getConstructorSuccessCode}
${getParent}
    ${dispatcherType} const * getDispatcher() const
    {
      VULKAN_HPP_ASSERT( m_dispatcher->getVkHeaderVersion() == VK_HEADER_VERSION );
      return ${getDispatcherReturn}m_dispatcher;
    }

    void swap( VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::${handleType} & rhs ) VULKAN_HPP_NOEXCEPT
    {
${swapMembers}
    }

${memberFunctionsDeclarations}

  private:
    ${memberVariables}
  };
${leave})";

    str += replaceWithMap( handleTemplate,
                           { { "assignmentOperator", assignmentOperator },
                             { "clearMembers", clearMembers },
                             { "copyConstructor", copyConstructor },
                             { "debugReportObjectType", debugReportObjectType },
                             { "dispatcherType", dispatcherType },
                             { "enter", enter },
                             { "getConstructorSuccessCode", getConstructorSuccessCode },
                             { "getDispatcherReturn", ( handleType == "Device" ) || ( handleType == "Instance" ) ? "&*" : "" },
                             { "getParent", getParent },
                             { "handleName", handleName },
                             { "handleType", handleType },
                             { "leave", leave },
                             { "memberFunctionsDeclarations", declarations },
                             { "memberVariables", memberVariables },
                             { "moveAssignmentInstructions", moveAssignmentInstructions },
                             { "moveConstructorInitializerList", moveConstructorInitializerList },
                             { "objTypeEnum", objTypeEnum },
                             { "releaseMembers", releaseMembers },
                             { "singularConstructors", singularConstructors },
                             { "swapMembers", swapMembers } } );

    if ( !arrayConstructors.empty() )
    {
      // it's a handle class with a friendly handles class
      const std::string handlesTemplate = R"(
${enter}  class ${handleType}s : public std::vector<VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::${handleType}>
  {
  public:
    ${arrayConstructors}
    ${handleType}s( std::nullptr_t ) {}

    ${handleType}s() = delete;
    ${handleType}s( ${handleType}s const & ) = delete;
    ${handleType}s( ${handleType}s && rhs ) = default;
    ${handleType}s & operator=( ${handleType}s const & ) = delete;
    ${handleType}s & operator=( ${handleType}s && rhs ) = default;
  };
${leave}
)";

      str +=
        replaceWithMap( handlesTemplate, { { "arrayConstructors", arrayConstructors }, { "enter", enter }, { "handleType", handleType }, { "leave", leave } } );
    }
  }
  return str;
}

std::string VulkanHppGenerator::generateRAIIHandleCommand( std::string const & command, size_t initialSkipCount, bool definition ) const
{
  std::string str;
  if ( m_RAIISpecialFunctions.find( command ) == m_RAIISpecialFunctions.end() )
  {
    str = generateCommand( command, getCommandData( command ), initialSkipCount, definition, true );
  }
  return str;
}

std::string VulkanHppGenerator::generateRAIIHandleCommandDeclarations( std::pair<std::string, HandleData> const & handle,
                                                                       std::set<std::string> const &              specialFunctions ) const
{
  std::string           functionDeclarations;
  std::set<std::string> listedCommands;  // some commands are listed with more than one extension !
  for ( auto const & feature : m_features )
  {
    std::vector<std::string> firstLevelCommands, secondLevelCommands;

    for ( auto const & require : feature.requireData )
    {
      for ( auto const & command : require.commands )
      {
        if ( specialFunctions.find( command ) == specialFunctions.end() )
        {
          if ( handle.second.commands.find( command ) != handle.second.commands.end() )
          {
            assert( listedCommands.find( command ) == listedCommands.end() );
            listedCommands.insert( command );
            firstLevelCommands.push_back( command );
          }
          else if ( handle.second.secondLevelCommands.find( command ) != handle.second.secondLevelCommands.end() )
          {
            assert( listedCommands.find( command ) == listedCommands.end() );
            listedCommands.insert( command );
            assert( !handle.first.empty() );
            secondLevelCommands.push_back( command );
          }
        }
      }
    }
    if ( !firstLevelCommands.empty() || !secondLevelCommands.empty() )
    {
      functionDeclarations += "\n  //=== " + feature.name + " ===\n";
      for ( auto const & command : firstLevelCommands )
      {
        functionDeclarations += generateRAIIHandleCommand( command, handle.first.empty() ? 0 : 1, false );
      }
      for ( auto const & command : secondLevelCommands )
      {
        assert( !handle.first.empty() );
        functionDeclarations += generateRAIIHandleCommand( command, 2, false );
      }
    }
  }

  for ( auto const & extension : m_extensions )
  {
    std::vector<std::string> firstLevelCommands, secondLevelCommands;
    for ( auto & req : extension.requireData )
    {
      for ( auto const & command : req.commands )
      {
        if ( ( specialFunctions.find( command ) == specialFunctions.end() ) && ( listedCommands.find( command ) == listedCommands.end() ) )
        {
          if ( handle.second.commands.find( command ) != handle.second.commands.end() )
          {
            listedCommands.insert( command );
            firstLevelCommands.push_back( command );
          }
          else if ( handle.second.secondLevelCommands.find( command ) != handle.second.secondLevelCommands.end() )
          {
            listedCommands.insert( command );
            secondLevelCommands.push_back( command );
          }
        }
      }
    }
    if ( !firstLevelCommands.empty() || !secondLevelCommands.empty() )
    {
      std::string handleProtect = getProtectFromType( handle.first );
      std::string titleProtect  = getProtectFromTitle( extension.name );
      std::string enter, leave;
      if ( !titleProtect.empty() && ( titleProtect != handleProtect ) )
      {
        std::tie( enter, leave ) = generateProtection( titleProtect );
      }
      functionDeclarations += "\n" + enter + "  //=== " + extension.name + " ===\n";
      for ( auto const & command : firstLevelCommands )
      {
        functionDeclarations += generateRAIIHandleCommand( command, handle.first.empty() ? 0 : 1, false );
      }
      for ( auto const & command : secondLevelCommands )
      {
        assert( !handle.first.empty() );
        functionDeclarations += generateRAIIHandleCommand( command, 2, false );
      }
      functionDeclarations += leave;
    }
  }
  return functionDeclarations;
}

std::string VulkanHppGenerator::generateRAIIHandleCommandEnhanced( std::string const &                       name,
                                                                   CommandData const &                       commandData,
                                                                   size_t                                    initialSkipCount,
                                                                   std::vector<size_t> const &               returnParams,
                                                                   std::map<size_t, VectorParamData> const & vectorParams,
                                                                   bool                                      definition,
                                                                   CommandFlavourFlags                       flavourFlags ) const
{
  bool singular = flavourFlags & CommandFlavourFlagBits::singular;

  std::set<size_t> skippedParams  = determineSkippedParams( commandData.params, initialSkipCount, vectorParams, returnParams, singular );
  std::set<size_t> singularParams = singular ? determineSingularParams( returnParams[0], vectorParams ) : std::set<size_t>();
  // special handling for vkGetMemoryHostPointerPropertiesEXT: here, we really need to stick with the const void * parameter !
  std::set<size_t> templatedParams = ( name == "vkGetMemoryHostPointerPropertiesEXT" ) ? std::set<size_t>() : determineVoidPointerParams( commandData.params );

  bool                     enumerating = determineEnumeration( vectorParams, returnParams );
  std::vector<std::string> dataTypes   = determineDataTypes( commandData.params, vectorParams, returnParams, templatedParams );
  std::string              dataType    = combineDataTypes( vectorParams, returnParams, enumerating, dataTypes, flavourFlags, true );

  std::string argumentTemplates = generateArgumentTemplates( commandData.params, returnParams, vectorParams, templatedParams, flavourFlags, true );
  std::string argumentList      = generateArgumentListEnhanced(
    commandData.params, returnParams, vectorParams, skippedParams, singularParams, templatedParams, definition, flavourFlags, false );
  std::string commandName = generateCommandName( name, commandData.params, initialSkipCount, flavourFlags );
  std::string nodiscard   = generateNoDiscard(
    !returnParams.empty() || ( ( commandData.returnType != "VkResult" ) && ( commandData.returnType != "void" ) ), 1 < commandData.successCodes.size(), false );
  std::pair<bool, std::map<size_t, std::vector<size_t>>> vectorSizeCheck =
    needsVectorSizeCheck( commandData.params, vectorParams, returnParams, singularParams, skippedParams );
  std::string noexceptString = generateNoExcept( commandData.errorCodes, returnParams, vectorParams, flavourFlags, vectorSizeCheck.first, true );
  std::string returnType     = generateReturnType( commandData, returnParams, vectorParams, flavourFlags, true, dataType );

  if ( definition )
  {
    std::string const definitionTemplate =
      R"(
  ${argumentTemplates}
  ${nodiscard} VULKAN_HPP_INLINE ${returnType} ${className}::${commandName}( ${argumentList} ) const ${noexcept}
  {
${functionPointerCheck}
${vectorSizeCheck}
    ${dataSizeChecks}
    ${dataDeclarations}
    ${callSequence}
    ${resultCheck}
    ${dataPreparation}
    ${returnStatement}
  }
)";

    std::string callSequence =
      generateCallSequence( name, commandData, returnParams, vectorParams, initialSkipCount, singularParams, templatedParams, flavourFlags, true );
    std::string className      = initialSkipCount ? stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" ) : "Context";
    std::string returnVariable = generateReturnVariable( commandData, returnParams, vectorParams, flavourFlags );
    std::string dataDeclarations =
      generateDataDeclarations( commandData, returnParams, vectorParams, templatedParams, flavourFlags, true, dataTypes, dataType, returnType, returnVariable );
    std::string dataPreparation =
      generateDataPreparation( commandData, initialSkipCount, returnParams, vectorParams, templatedParams, flavourFlags, enumerating );
    std::string dataSizeChecks  = generateDataSizeChecks( commandData, returnParams, dataTypes, vectorParams, templatedParams, singular );
    std::string resultCheck     = generateResultCheck( commandData, className, "::", commandName, enumerating );
    std::string returnStatement = generateReturnStatement( name,
                                                           commandData,
                                                           returnVariable,
                                                           returnType,
                                                           dataType,
                                                           initialSkipCount,
                                                           returnParams.empty() ? INVALID_INDEX : returnParams[0],
                                                           flavourFlags,
                                                           enumerating,
                                                           true );
    std::string vectorSizeCheckString =
      vectorSizeCheck.first ? generateRAIIHandleVectorSizeCheck( name, commandData, initialSkipCount, vectorSizeCheck.second, skippedParams ) : "";

    return replaceWithMap( definitionTemplate,
                           { { "argumentList", argumentList },
                             { "argumentTemplates", argumentTemplates },
                             { "callSequence", callSequence },
                             { "className", className },
                             { "commandName", commandName },
                             { "dataDeclarations", dataDeclarations },
                             { "dataPreparation", dataPreparation },
                             { "dataSizeChecks", dataSizeChecks },
                             { "functionPointerCheck", generateFunctionPointerCheck( name, commandData.requiredBy ) },
                             { "nodiscard", nodiscard },
                             { "noexcept", noexceptString },
                             { "resultCheck", resultCheck },
                             { "returnStatement", returnStatement },
                             { "returnType", returnType },
                             { "vectorSizeCheck", vectorSizeCheckString } } );
  }
  else
  {
    std::string const declarationTemplate =
      R"(
    ${argumentTemplates}
    ${nodiscard} ${returnType} ${commandName}( ${argumentList} ) const ${noexcept};
)";

    return replaceWithMap( declarationTemplate,
                           { { "argumentList", argumentList },
                             { "argumentTemplates", argumentTemplates },
                             { "commandName", commandName },
                             { "nodiscard", nodiscard },
                             { "noexcept", noexceptString },
                             { "returnType", returnType } } );
  }
}

std::string VulkanHppGenerator::generateRAIIHandleCommandFactory( std::string const &                       name,
                                                                  CommandData const &                       commandData,
                                                                  size_t                                    initialSkipCount,
                                                                  std::vector<size_t> const &               returnParams,
                                                                  std::map<size_t, VectorParamData> const & vectorParams,
                                                                  bool                                      definition,
                                                                  CommandFlavourFlags                       flavourFlags ) const
{
  bool singular = flavourFlags & CommandFlavourFlagBits::singular;

  assert( isHandleType( commandData.params[returnParams.back()].type.type ) );
  assert( ( returnParams.size() == 1 ) ||
          ( ( returnParams.size() == 2 ) && ( vectorParams.size() == 1 ) && ( returnParams[0] == vectorParams.begin()->second.lenParam ) &&
            ( returnParams[1] == vectorParams.begin()->first ) ) );

  std::set<size_t> skippedParams  = determineSkippedParams( commandData.params, initialSkipCount, vectorParams, returnParams, singular );
  std::set<size_t> singularParams = singular ? determineSingularParams( returnParams.back(), vectorParams ) : std::set<size_t>();
  std::string      argumentList   = generateRAIIHandleCommandFactoryArgumentList( commandData.params, skippedParams, definition, singular );
  std::string      commandName    = generateCommandName( name, commandData.params, initialSkipCount, flavourFlags );
  std::string      handleType     = stripPostfix( commandData.params[returnParams.back()].type.compose( "VULKAN_HPP_RAII_NAMESPACE" ), " *" );
  std::string      returnType     = handleType;
  if ( ( vectorParams.find( returnParams.back() ) != vectorParams.end() ) && !singular )
  {
    returnType = "std::vector<" + handleType + ">";
    handleType += "s";
  }

  if ( definition )
  {
    std::string callArguments = generateCallArgumentsRAIIFactory( commandData.params, initialSkipCount, skippedParams, singularParams );
    std::string className     = initialSkipCount ? stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" ) : "Context";

    std::string const definitionTemplate =
      R"(
  VULKAN_HPP_NODISCARD VULKAN_HPP_INLINE ${returnType} ${className}::${commandName}( ${argumentList} ) const
  {
    return ${handleType}( ${callArguments} );
  }
)";

    return replaceWithMap( definitionTemplate,
                           { { "argumentList", argumentList },
                             { "callArguments", callArguments },
                             { "className", className },
                             { "commandName", commandName },
                             { "handleType", handleType },
                             { "returnType", returnType } } );
  }
  else
  {
    std::string const declarationTemplate =
      R"(
  VULKAN_HPP_NODISCARD ${returnType} ${commandName}( ${argumentList} ) const;
)";

    return replaceWithMap( declarationTemplate, { { "argumentList", argumentList }, { "commandName", commandName }, { "returnType", returnType } } );
  }
}

std::string VulkanHppGenerator::generateRAIIHandleCommandFactoryArgumentList( std::vector<ParamData> const & params,
                                                                              std::set<size_t> const &       skippedParams,
                                                                              bool                           definition,
                                                                              bool                           singular ) const
{
  std::string arguments;
  bool        encounteredArgument = false;
  for ( size_t i = 0; i < params.size(); ++i )
  {
    if ( skippedParams.find( i ) == skippedParams.end() )
    {
      if ( encounteredArgument )
      {
        arguments += ", ";
      }
      arguments += generateRAIIHandleConstructorArgument( params[i], definition, singular, false );
      encounteredArgument = true;
    }
  }
  return arguments;
}

std::pair<std::string, std::string>
  VulkanHppGenerator::generateRAIIHandleConstructor( std::pair<std::string, HandleData> const &                             handle,
                                                     std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                     std::string const &                                                    enter,
                                                     std::string const &                                                    leave ) const
{
  std::string singularConstructor, arrayConstructor;
  if ( constructorIt->second.returnType == "VkResult" )
  {
    std::tie( singularConstructor, arrayConstructor ) = generateRAIIHandleConstructorResult( handle, constructorIt, enter, leave );
  }
  else if ( constructorIt->second.returnType == "void" )
  {
    std::tie( singularConstructor, arrayConstructor ) = generateRAIIHandleConstructorVoid( handle, constructorIt, enter, leave );
  }
  if ( singularConstructor.empty() && arrayConstructor.empty() )
  {
    throw std::runtime_error( "Never encountered a function like <" + constructorIt->first + "> !" );
  }
  return std::make_pair( singularConstructor, arrayConstructor );
}

std::pair<std::string, std::string>
  VulkanHppGenerator::generateRAIIHandleConstructor1Return2Vector( std::pair<std::string, HandleData> const &         handle,
                                                                   std::map<std::string, CommandData>::const_iterator constructorIt,
                                                                   std::string const &                                enter,
                                                                   std::string const &                                leave,
                                                                   size_t                                             returnParam,
                                                                   std::map<size_t, VectorParamData> const &          vectorParams ) const
{
  if ( returnParam == std::next( vectorParams.begin() )->first )
  {
    if ( vectorParams.begin()->second.lenParam == std::next( vectorParams.begin() )->second.lenParam )
    {
      if ( constructorIt->second.params[vectorParams.begin()->second.lenParam].type.type == "uint32_t" )
      {
        if ( ( constructorIt->second.params[vectorParams.begin()->first].type.type != "void" ) &&
             !isHandleType( constructorIt->second.params[vectorParams.begin()->first].type.type ) )
        {
          std::string singularConstructor;
          auto        lenParamIt    = constructorIt->second.params.begin() + vectorParams.begin()->second.lenParam;
          auto        handleParamIt = constructorIt->second.params.begin() + std::next( vectorParams.begin() )->first;
          if ( !checkEquivalentSingularConstructor( handle.second.constructorIts, constructorIt, lenParamIt ) )
          {
            singularConstructor = generateRAIIHandleConstructorVectorSingular( handle, constructorIt, handleParamIt, enter, leave );
          }
          return std::make_pair( singularConstructor, generateRAIIHandleConstructorVector( handle, constructorIt, handleParamIt, enter, leave ) );
        }
      }
    }
  }
  return std::make_pair( "", "" );
}

std::pair<std::string, std::string> VulkanHppGenerator::generateRAIIHandleConstructors( std::pair<std::string, HandleData> const & handle ) const
{
  auto [enter, leave] = generateProtection( getProtectFromType( handle.first ) );

  std::string singularConstructors, arrayConstructors;
  for ( auto constructorIt : handle.second.constructorIts )
  {
    // there is a non-const parameter with handle type : the to-be-constructed handle

    // check for additional enter/leave guards for the constructors
    auto [constructorEnter, constructorLeave] = generateProtection( getProtectFromTitles( constructorIt->second.requiredBy ) );
    if ( constructorEnter == enter )
    {
      constructorEnter.clear();
      constructorLeave.clear();
    }

    auto [singularConstructor, arrayConstructor] = generateRAIIHandleConstructor( handle, constructorIt, constructorEnter, constructorLeave );
    arrayConstructors += arrayConstructor;
    singularConstructors += singularConstructor;
  }
  singularConstructors += generateRAIIHandleConstructorTakeOwnership( handle );
  return std::make_pair( singularConstructors, arrayConstructors );
}

std::string VulkanHppGenerator::generateRAIIHandleConstructorArgument( ParamData const & param, bool definition, bool singular, bool takesOwnership ) const
{
  std::string argument;
  if ( param.type.isConstPointer() )
  {
    assert( param.type.type.starts_with( "Vk" ) );
    assert( param.name.starts_with( "p" ) );
    std::string argumentName = startLowerCase( stripPrefix( param.name, "p" ) );
    std::string argumentType = generateNamespacedType( param.type.type );
    if ( param.optional )
    {
      assert( param.lenExpression.empty() );
      argument = "VULKAN_HPP_NAMESPACE::Optional<const " + argumentType + "> " + argumentName + ( definition ? "" : " = nullptr" );
    }
    else if ( param.lenExpression.empty() )
    {
      argument = argumentType + " const & " + argumentName;
    }
    else if ( singular )
    {
      argument = argumentType + " const & " + stripPluralS( argumentName );
    }
    else
    {
      argument = "VULKAN_HPP_NAMESPACE::ArrayProxy<" + argumentType + "> const & " + argumentName;
    }
  }
  else if ( specialPointerTypes.find( param.type.type ) != specialPointerTypes.end() )
  {
    assert( !param.optional );
    assert( param.type.isNonConstPointer() );
    argument = param.type.type + " & " + param.name;
  }
  else if ( ( param.type.isValue() ) && isHandleType( param.type.type ) )
  {
    if ( takesOwnership )
    {
      assert( !param.optional );
      argument = param.type.type + " " + param.name;
    }
    else
    {
      argument = "VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::" + stripPrefix( param.type.type, "Vk" );
      if ( param.optional )
      {
        argument = "VULKAN_HPP_NAMESPACE::Optional<const " + argument + ">";
      }
      argument += " const & " + param.name;
    }
  }
  else
  {
    assert( !param.optional );
    if ( param.arraySizes.empty() )
    {
      argument = param.type.compose( "VULKAN_HPP_NAMESPACE" ) + " ";
    }
    else
    {
      argument = generateStandardArray( param.type.compose( "VULKAN_HPP_NAMESPACE" ), param.arraySizes ) + " const & ";
    }
    argument += param.name;
  }
  return argument;
}

std::string VulkanHppGenerator::generateRAIIHandleConstructorArguments( std::pair<std::string, HandleData> const &                             handle,
                                                                        std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                                        bool                                                                   singular,
                                                                        bool takesOwnership ) const
{
  auto [parentType, parentName] = getParentTypeAndName( handle );

  std::string arguments = "VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::" + parentType + " const & " + parentName;
  if ( takesOwnership )
  {
    arguments += ", " + handle.first + " " + generateRAIIHandleConstructorParamName( handle.first, handle.second.destructorIt );
  }

  if ( constructorIt != m_commands.end() )
  {
    parentType = "Vk" + parentType;
    bool skip  = skipLeadingGrandParent( handle );
    for ( size_t i = skip ? 1 : 0; i < constructorIt->second.params.size(); i++ )
    {
      ParamData const & param = constructorIt->second.params[i];
      // filter parent and handle type
      if ( ( param.type.type != parentType ) && ( param.type.type != handle.first ) )
      {
        // the specialPointerTypes are considered const-pointers!
        if ( param.type.isNonConstPointer() && ( specialPointerTypes.find( param.type.type ) == specialPointerTypes.end() ) )
        {
          // this is supposed to be the returned size on an enumeration function!
#if !defined( NDEBUG )
          assert( param.type.type == "uint32_t" );
          auto typeIt = std::find_if( constructorIt->second.params.begin(),
                                      constructorIt->second.params.end(),
                                      [&handle]( ParamData const & pd ) { return pd.type.type == handle.first; } );
          assert( typeIt != constructorIt->second.params.end() );
          assert( typeIt->lenExpression == param.name );
#endif
          continue;
        }
        else if ( std::find_if( constructorIt->second.params.begin(),
                                constructorIt->second.params.end(),
                                [&param]( ParamData const & pd ) { return pd.lenExpression == param.name; } ) != constructorIt->second.params.end() )
        {
          // this is the len of an other parameter, which will be mapped to an ArrayProxy
          assert( param.type.isValue() && ( param.type.type == "uint32_t" ) );
          assert( param.arraySizes.empty() && param.lenExpression.empty() && !param.optional );
          continue;
        }
        arguments += ", " + generateRAIIHandleConstructorArgument( param, false, singular, takesOwnership );
      }
    }
  }
  return arguments;
}

std::string
  VulkanHppGenerator::generateRAIIHandleConstructorCallArguments( std::pair<std::string, HandleData> const &                             handle,
                                                                  std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                                  bool                     nonConstPointerAsNullptr,
                                                                  std::set<size_t> const & singularParams,
                                                                  bool                     allocatorIsMemberVariable ) const
{
  std::string arguments;
  bool        encounteredArgument = false;
  size_t      i                   = 0;
  if ( skipLeadingGrandParent( handle ) )
  {
    assert( ( 1 < constructorIt->second.params.size() ) && ( m_handles.find( constructorIt->second.params[0].type.type ) != m_handles.end() ) &&
            ( m_handles.find( constructorIt->second.params[1].type.type ) != m_handles.end() ) );
    arguments += "static_cast<" + constructorIt->second.params[0].type.type + ">( " + constructorIt->second.params[1].name + ".get" +
                 stripPrefix( constructorIt->second.params[0].type.type, "Vk" ) + "() )";
    encounteredArgument = true;
    i                   = 1;
  }
  for ( ; i < constructorIt->second.params.size(); ++i )
  {
    ParamData const & param = constructorIt->second.params[i];
    if ( encounteredArgument )
    {
      arguments += ", ";
    }
    if ( param.type.type == handle.first )
    {
      assert( param.type.isNonConstPointer() && param.arraySizes.empty() );
      if ( param.lenExpression.empty() || !singularParams.empty() )
      {
        assert( !param.optional );
        assert( singularParams.empty() || ( param.lenExpression == constructorIt->second.params[*singularParams.begin()].name ) );
        std::string paramName = generateRAIIHandleConstructorParamName( handle.first, handle.second.destructorIt );
        arguments += "reinterpret_cast<" + handle.first + "*>( &m_" + paramName + " )";
      }
      else if ( nonConstPointerAsNullptr )
      {
        arguments += "nullptr";
      }
      else
      {
        arguments += startLowerCase( stripPrefix( param.name, "p" ) ) + ".data()";
      }
    }
    else if ( param.type.type == "VkAllocationCallbacks" )
    {
      assert( param.optional );
      if ( allocatorIsMemberVariable )
      {
        arguments += "reinterpret_cast<const VkAllocationCallbacks *>( m_allocator )";
      }
      else
      {
        arguments += "reinterpret_cast<const VkAllocationCallbacks *>(static_cast<const VULKAN_HPP_NAMESPACE::AllocationCallbacks *>( allocator ) )";
      }
    }
    else if ( m_handles.find( param.type.type ) != m_handles.end() )
    {
      assert( param.type.isValue() && param.arraySizes.empty() && param.lenExpression.empty() );
      if ( param.optional )
      {
        arguments += param.name + " ? static_cast<" + param.type.type + ">( **" + param.name + " ) : 0";
      }
      else
      {
        arguments += "static_cast<" + param.type.type + ">( *" + param.name + " )";
      }
    }
    else
    {
      assert( !param.optional );
      arguments += generateCallArgumentEnhanced( constructorIt->second.params, i, nonConstPointerAsNullptr, singularParams, {} );
    }
    encounteredArgument = true;
  }
  return arguments;
}

std::string VulkanHppGenerator::generateRAIIHandleConstructorEnumerate( std::pair<std::string, HandleData> const &                             handle,
                                                                        std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                                        std::vector<ParamData>::const_iterator                                 handleParamIt,
                                                                        std::vector<ParamData>::const_iterator                                 lenParamIt,
                                                                        std::string const &                                                    enter,
                                                                        std::string const &                                                    leave ) const
{
  std::string handleConstructorArguments = generateRAIIHandleSingularConstructorArguments( handle, constructorIt );
  std::string handleType                 = stripPrefix( handle.first, "Vk" );
  std::string dispatcherType             = hasParentHandle( handle.first, "VkDevice" ) ? "VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::DeviceDispatcher"
                                                                                       : "VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::InstanceDispatcher";

  const std::string constructorTemplate =
    R"(
${enter}    ${handleType}s( ${constructorArguments} )
    {
      ${dispatcherType} const * dispatcher = ${parentName}.getDispatcher();
      std::vector<${vectorElementType}> ${vectorName};
      ${counterType} ${counterName};
      VULKAN_HPP_NAMESPACE::Result result;
      do
      {
        result = static_cast<VULKAN_HPP_NAMESPACE::Result>( dispatcher->${constructorCall}( ${firstCallArguments} ) );
        if ( ( result == VULKAN_HPP_NAMESPACE::Result::eSuccess ) && ${counterName} )
        {
          ${vectorName}.resize( ${counterName} );
          result = static_cast<VULKAN_HPP_NAMESPACE::Result>( dispatcher->${constructorCall}( ${secondCallArguments} ) );
        }
      } while ( result == VULKAN_HPP_NAMESPACE::Result::eIncomplete );
      if ( result == VULKAN_HPP_NAMESPACE::Result::eSuccess )
      {
        VULKAN_HPP_ASSERT( ${counterName} <= ${vectorName}.size() );
        this->reserve( ${counterName} );
        for ( auto const & ${handleName} : ${vectorName} )
        {
          this->emplace_back( ${parentName}, ${handleConstructorArguments} );
        }
      }
      else
      {
        throwResultException( result, "${constructorCall}" );
      }
    }
${leave})";

  return replaceWithMap( constructorTemplate,
                         { { "constructorArguments", generateRAIIHandleConstructorArguments( handle, constructorIt, false, false ) },
                           { "constructorCall", constructorIt->first },
                           { "counterName", startLowerCase( stripPrefix( lenParamIt->name, "p" ) ) },
                           { "counterType", lenParamIt->type.type },
                           { "dispatcherType", dispatcherType },
                           { "enter", enter },
                           { "firstCallArguments", generateRAIIHandleConstructorCallArguments( handle, constructorIt, true, {}, true ) },
                           { "handleConstructorArguments", handleConstructorArguments },
                           { "handleName", startLowerCase( handleType ) },
                           { "handleType", handleType },
                           { "leave", leave },
                           { "parentName", constructorIt->second.params.front().name },
                           { "secondCallArguments", generateRAIIHandleConstructorCallArguments( handle, constructorIt, false, {}, true ) },
                           { "vectorElementType", handleParamIt->type.type },
                           { "vectorName", startLowerCase( stripPrefix( handleParamIt->name, "p" ) ) } } );
}

std::string
  VulkanHppGenerator::generateRAIIHandleConstructorInitializationList( std::pair<std::string, HandleData> const &                             handle,
                                                                       std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                                       std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator destructorIt,
                                                                       bool takesOwnership ) const
{
  auto [parentType, parentName] = getParentTypeAndName( handle );
  std::string handleName        = generateRAIIHandleConstructorParamName( handle.first, destructorIt );

  std::string initializationList;
  if ( destructorIt != m_commands.end() )
  {
    for ( auto destructorParam : destructorIt->second.params )
    {
      if ( destructorParam.type.type == "Vk" + parentType )
      {
        initializationList += "m_" + parentName + "( *" + parentName + " ), ";
      }
      else if ( destructorParam.type.type == handle.first )
      {
        if ( takesOwnership )
        {
          initializationList += "m_" + handleName + "( " + handleName + " ), ";
        }
      }
      else if ( destructorParam.type.type == "VkAllocationCallbacks" )
      {
        assert( destructorParam.type.isConstPointer() && destructorParam.arraySizes.empty() && destructorParam.lenExpression.empty() &&
                destructorParam.optional );
        initializationList += "m_allocator( static_cast<const VULKAN_HPP_NAMESPACE::AllocationCallbacks *>( allocator ) ), ";
      }
      else if ( isHandleType( destructorParam.type.type ) )
      {
        assert( destructorParam.type.isValue() && destructorParam.arraySizes.empty() && destructorParam.lenExpression.empty() && !destructorParam.optional );
        initializationList += "m_" + destructorParam.name + "( ";
        auto constructorParamIt = std::find_if( constructorIt->second.params.begin(),
                                                constructorIt->second.params.end(),
                                                [&destructorParam]( ParamData const & pd ) { return pd.type.type == destructorParam.type.type; } );
        if ( constructorParamIt != constructorIt->second.params.end() )
        {
          assert( constructorParamIt->type.isValue() && constructorParamIt->arraySizes.empty() && constructorParamIt->lenExpression.empty() &&
                  !constructorParamIt->optional );
          if ( constructorParamIt->type.type == "Vk" + parentType )
          {
            initializationList += "*";
          }
          initializationList += constructorParamIt->name;
        }
        else
        {
#if !defined( NDEBUG )
          bool found = false;
#endif
          for ( auto constructorParam : constructorIt->second.params )
          {
            auto structureIt = m_structs.find( constructorParam.type.type );
            if ( structureIt != m_structs.end() )
            {
              auto structureMemberIt = findStructMemberItByType( destructorParam.type.type, structureIt->second.members );
              if ( structureMemberIt != structureIt->second.members.end() )
              {
                assert( constructorParam.type.isConstPointer() && constructorParam.arraySizes.empty() && constructorParam.lenExpression.empty() &&
                        !constructorParam.optional );
                initializationList += startLowerCase( stripPrefix( constructorParam.name, "p" ) ) + "." + structureMemberIt->name;
#if !defined( NDEBUG )
                found = true;
#endif
                break;
              }
            }
          }
          assert( found );
        }
        initializationList += " ), ";
      }
      else
      {
        // we can ignore all other parameters here !
      }
    }
  }
  else
  {
    if ( !handle.second.secondLevelCommands.empty() )
    {
      assert( !handle.second.constructorIts.empty() );
#if !defined( NDEBUG )
      auto constructorCommandIt = m_commands.find( handle.second.constructorIts.front()->first );
#endif
      assert( ( constructorCommandIt != m_commands.end() ) && ( 1 < constructorCommandIt->second.params.size() ) );
      assert( std::next( constructorCommandIt->second.params.begin() )->type.type == "Vk" + parentType );

      auto commandIt = m_commands.find( *handle.second.secondLevelCommands.begin() );
      assert( ( commandIt != m_commands.end() ) && ( 1 < commandIt->second.params.size() ) );
      assert( commandIt->second.params.front().type.type == constructorCommandIt->second.params.front().type.type );
      assert( std::next( commandIt->second.params.begin() )->type.type == handle.first );

      std::string grandParentType = stripPrefix( commandIt->second.params.front().type.type, "Vk" );
      initializationList += "m_" + startLowerCase( grandParentType ) + "( " + parentName + ".get" + grandParentType + "() ), ";
    }
    if ( takesOwnership )
    {
      initializationList += "m_" + handleName + "( " + handleName + " ), ";
    }
  }
  return initializationList.empty() ? initializationList : initializationList.substr( 0, initializationList.size() - 2 );
}

std::string VulkanHppGenerator::generateRAIIHandleConstructorParamName( std::string const &                                type,
                                                                        std::map<std::string, CommandData>::const_iterator destructorIt ) const
{
  if ( destructorIt != m_commands.end() )
  {
    auto destructorParamIt = std::find_if( destructorIt->second.params.begin(),
                                           destructorIt->second.params.end(),
                                           [&type]( ParamData const & destructorParam ) { return destructorParam.type.type == type; } );
    if ( destructorParamIt != destructorIt->second.params.end() )
    {
      assert( std::find_if( std::next( destructorParamIt ),
                            destructorIt->second.params.end(),
                            [&type]( ParamData const & destructorParam ) { return destructorParam.type.type == type; } ) == destructorIt->second.params.end() );
      if ( !destructorParamIt->type.isValue() )
      {
        return startLowerCase( stripPrefix( stripPluralS( destructorParamIt->name ), "p" ) );
      }
      else
      {
        return destructorParamIt->name;
      }
    }
  }
  return startLowerCase( stripPrefix( type, "Vk" ) );
}

std::pair<std::string, std::string> VulkanHppGenerator::generateRAIIHandleConstructorResult( std::pair<std::string, HandleData> const &         handle,
                                                                                             std::map<std::string, CommandData>::const_iterator constructorIt,
                                                                                             std::string const &                                enter,
                                                                                             std::string const &                                leave ) const
{
  assert( !constructorIt->second.successCodes.empty() );
  assert( constructorIt->second.successCodes[0] == "VK_SUCCESS" );
  switch ( constructorIt->second.successCodes.size() )
  {
    case 1:
      if ( !constructorIt->second.errorCodes.empty() )
      {
        std::vector<size_t> returnParams = determineReturnParams( constructorIt->second.params );
        if ( returnParams.size() == 1 )
        {
          assert( isHandleType( constructorIt->second.params[returnParams[0]].type.type ) );
          std::map<size_t, VectorParamData> vectorParams = determineVectorParams( constructorIt->second.params );
          switch ( vectorParams.size() )
          {
            case 0:
              return std::make_pair( generateRAIIHandleConstructorResultSingleSuccessWithErrors1Return0Vector( handle, constructorIt, enter, leave ), "" );
            case 1:
              if ( returnParams[0] == vectorParams.begin()->first )
              {
                if ( isLenByStructMember( constructorIt->second.params[vectorParams.begin()->first].lenExpression,
                                          constructorIt->second.params[vectorParams.begin()->second.lenParam] ) )
                {
                  auto handleParamIt = constructorIt->second.params.begin() + returnParams[0];
                  return std::make_pair( "", generateRAIIHandleConstructorVector( handle, constructorIt, handleParamIt, enter, leave ) );
                }
              }
              break;
            case 2: return generateRAIIHandleConstructor1Return2Vector( handle, constructorIt, enter, leave, returnParams[0], vectorParams );
          }
        }
      }
      break;
    case 2:
      if ( !constructorIt->second.errorCodes.empty() )
      {
        std::vector<size_t> returnParams = determineReturnParams( constructorIt->second.params );
        switch ( returnParams.size() )
        {
          case 1:
            assert( isHandleType( constructorIt->second.params[returnParams[0]].type.type ) );
            {
              std::map<size_t, VectorParamData> vectorParams = determineVectorParams( constructorIt->second.params );
              if ( vectorParams.size() == 2 )
              {
                return generateRAIIHandleConstructor1Return2Vector( handle, constructorIt, enter, leave, returnParams[0], vectorParams );
              }
            }
            break;
          case 2:
            if ( constructorIt->second.params[returnParams[0]].type.type == "uint32_t" )
            {
              assert( isHandleType( constructorIt->second.params[returnParams[1]].type.type ) );
              std::map<size_t, VectorParamData> vectorParams = determineVectorParams( constructorIt->second.params );
              if ( vectorParams.size() == 1 )
              {
                if ( returnParams[0] == vectorParams.begin()->second.lenParam )
                {
                  assert( returnParams[1] == vectorParams.begin()->first );
                  assert( constructorIt->second.successCodes[1] == "VK_INCOMPLETE" );
                  auto lenParamIt    = constructorIt->second.params.begin() + returnParams[0];
                  auto handleParamIt = constructorIt->second.params.begin() + returnParams[1];
                  return std::make_pair( "", generateRAIIHandleConstructorEnumerate( handle, constructorIt, handleParamIt, lenParamIt, enter, leave ) );
                }
              }
            }
            break;
        }
      }
      break;
    case 4:
      if ( !constructorIt->second.errorCodes.empty() )
      {
        std::vector<size_t> returnParams = determineReturnParams( constructorIt->second.params );
        if ( returnParams.size() == 1 )
        {
          assert( isHandleType( constructorIt->second.params[returnParams[0]].type.type ) );
          std::map<size_t, VectorParamData> vectorParams = determineVectorParams( constructorIt->second.params );
          if ( vectorParams.size() == 2 )
          {
            return generateRAIIHandleConstructor1Return2Vector( handle, constructorIt, enter, leave, returnParams[0], vectorParams );
          }
        }
      }
      break;
  }
  return std::make_pair( "", "" );
}

std::string VulkanHppGenerator::generateRAIIHandleConstructorResultSingleSuccessWithErrors1Return0Vector(
  std::pair<std::string, HandleData> const &                             handle,
  std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
  std::string const &                                                    enter,
  std::string const &                                                    leave ) const
{
  auto [parentType, parentName] = getParentTypeAndName( handle );

  std::string getDispatcher = parentName + ".getDispatcher()";
  std::string dispatcherInitializer, dispatcherInit;
  if ( ( handle.first != "VkInstance" ) && ( handle.first != "VkDevice" ) )
  {
    dispatcherInitializer = "m_dispatcher( " + getDispatcher + " )";
  }
  else
  {
    std::string handleType = stripPrefix( handle.first, "Vk" );
    dispatcherInit         = "\n        m_dispatcher.reset( new VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::" + handleType + "Dispatcher( " + parentName +
                     ".getDispatcher()->vkGet" + handleType + "ProcAddr, static_cast<" + handle.first + ">( m_" + startLowerCase( handleType ) + " ) ) );";
  }

  std::string constructorArguments = generateRAIIHandleConstructorArguments( handle, constructorIt, false, false );

  std::string callArguments = generateRAIIHandleConstructorCallArguments( handle, constructorIt, false, {}, handle.second.destructorIt != m_commands.end() );

  std::string initializationList = generateRAIIHandleConstructorInitializationList( handle, constructorIt, handle.second.destructorIt, false );
  if ( !initializationList.empty() && !dispatcherInitializer.empty() )
  {
    initializationList += ", ";
  }

  const std::string constructorTemplate =
    R"(
${enter}    ${handleType}( ${constructorArguments} )
      : ${initializationList}${dispatcherInitializer}
    {
      VULKAN_HPP_NAMESPACE::Result result = static_cast<VULKAN_HPP_NAMESPACE::Result>( ${getDispatcher}->${constructorCall}( ${callArguments} ) );
      if ( ${failureCheck} )
      {
        throwResultException( result, "${constructorCall}" );
      }${dispatcherInit}
    }
${leave})";

  return replaceWithMap( constructorTemplate,
                         { { "callArguments", callArguments },
                           { "constructorArguments", constructorArguments },
                           { "constructorCall", constructorIt->first },
                           { "dispatcherInitializer", dispatcherInitializer },
                           { "dispatcherInit", dispatcherInit },
                           { "enter", enter },
                           { "failureCheck", generateFailureCheck( constructorIt->second.successCodes ) },
                           { "getDispatcher", getDispatcher },
                           { "leave", leave },
                           { "handleType", stripPrefix( handle.first, "Vk" ) },
                           { "initializationList", initializationList } } );
}

std::string VulkanHppGenerator::generateRAIIHandleConstructorTakeOwnership( std::pair<std::string, HandleData> const & handle ) const
{
  std::string handleType = stripPrefix( handle.first, "Vk" );
  std::string handleName = startLowerCase( handleType );

  auto [parentType, parentName] = getParentTypeAndName( handle );

  std::string constructorArguments = generateRAIIHandleConstructorArguments( handle, handle.second.destructorIt, false, true );
  std::string initializationList   = generateRAIIHandleConstructorInitializationList( handle, handle.second.destructorIt, handle.second.destructorIt, true );
  assert( !handle.second.constructorIts.empty() );
  if ( 1 < handle.second.constructorIts[0]->second.successCodes.size() && ( handle.second.constructorIts[0]->second.successCodes[1] != "VK_INCOMPLETE" ) )
  {
#if !defined( NDEBUG )
    for ( size_t i = 1; i < handle.second.constructorIts.size(); ++i )
    {
      assert( 1 < handle.second.constructorIts[i]->second.successCodes.size() );
    }
#endif
    constructorArguments += ", VULKAN_HPP_NAMESPACE::Result successCode = VULKAN_HPP_NAMESPACE::Result::eSuccess";
    if ( isMultiSuccessCodeConstructor( handle.second.constructorIts ) )
    {
      initializationList += ", m_constructorSuccessCode( successCode )";
    }
  }

  std::string dispatcherInitializer;
  if ( ( handle.first != "VkInstance" ) && ( handle.first != "VkDevice" ) )
  {
    dispatcherInitializer = "m_dispatcher( " + parentName + ".getDispatcher() )";
  }
  if ( !initializationList.empty() && !dispatcherInitializer.empty() )
  {
    initializationList += ", ";
  }

  std::string dispatcherInit;
  if ( ( handle.first == "VkDevice" ) || ( handle.first == "VkInstance" ) )
  {
    dispatcherInit = "\n        m_dispatcher.reset( new VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::" + handleType + "Dispatcher( " + parentName +
                     ".getDispatcher()->vkGet" + handleType + "ProcAddr, static_cast<" + handle.first + ">( m_" + startLowerCase( handleType ) + " ) ) );";
  }

  const std::string constructorTemplate =
    R"(
    ${handleType}( ${constructorArguments} )
      : ${initializationList}${dispatcherInitializer}
    {${dispatcherInit}}
)";

  return replaceWithMap( constructorTemplate,
                         { { "constructorArguments", constructorArguments },
                           { "dispatcherInitializer", dispatcherInitializer },
                           { "dispatcherInit", dispatcherInit },
                           { "handleType", handleType },
                           { "initializationList", initializationList } } );
}

std::string VulkanHppGenerator::generateRAIIHandleConstructorVector( std::pair<std::string, HandleData> const &                             handle,
                                                                     std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                                     std::vector<ParamData>::const_iterator                                 handleParamIt,
                                                                     std::string const &                                                    enter,
                                                                     std::string const &                                                    leave ) const
{
  std::string vectorSize;
  auto        lenIt = std::find_if( constructorIt->second.params.begin(),
                             constructorIt->second.params.end(),
                             [&handleParamIt]( ParamData const & pd ) { return pd.name == handleParamIt->lenExpression; } );
  if ( lenIt == constructorIt->second.params.end() )
  {
    std::vector<std::string> lenParts = tokenize( handleParamIt->lenExpression, "->" );
    assert( lenParts.size() == 2 );
    lenIt = std::find_if(
      constructorIt->second.params.begin(), constructorIt->second.params.end(), [&lenParts]( ParamData const & pd ) { return pd.name == lenParts[0]; } );
#if !defined( NDEBUG )
    assert( lenIt != constructorIt->second.params.end() );
    auto structureIt = m_structs.find( lenIt->type.type );
    assert( structureIt != m_structs.end() );
    assert( isStructMember( lenParts[1], structureIt->second.members ) );
    assert( constructorIt->second.successCodes.size() == 1 );
#endif
    vectorSize = startLowerCase( stripPrefix( lenParts[0], "p" ) ) + "." + lenParts[1];
  }
  else
  {
    auto arrayIt =
      std::find_if( constructorIt->second.params.begin(),
                    constructorIt->second.params.end(),
                    [&lenIt, &handleParamIt]( ParamData const & pd ) { return ( pd.lenExpression == lenIt->name ) && ( pd.name != handleParamIt->name ); } );
    assert( arrayIt != constructorIt->second.params.end() );
    vectorSize = startLowerCase( stripPrefix( arrayIt->name, "p" ) ) + ".size()";
  }

  std::string handleConstructorArguments = generateRAIIHandleSingularConstructorArguments( handle, constructorIt );
  std::string handleType                 = stripPrefix( handle.first, "Vk" );
  std::string successCodePassToElement   = ( 1 < constructorIt->second.successCodes.size() ) ? ", result" : "";

  const std::string constructorTemplate =
    R"(
${enter}    ${handleType}s( ${constructorArguments} )
    {
      VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::DeviceDispatcher const * dispatcher = ${parentName}.getDispatcher();
      std::vector<${vectorElementType}> ${vectorName}( ${vectorSize} );
      VULKAN_HPP_NAMESPACE::Result result = static_cast<VULKAN_HPP_NAMESPACE::Result>( dispatcher->${constructorCall}( ${callArguments} ) );
      if ( ${successCheck} )
      {
        this->reserve( ${vectorSize} );
        for ( auto const & ${handleName} : ${vectorName} )
        {
          this->emplace_back( ${parentName}, ${handleConstructorArguments}${successCodePassToElement} );
        }
      }
      else
      {
        throwResultException( result, "${constructorCall}" );
      }
    }
${leave})";

  return replaceWithMap( constructorTemplate,
                         { { "callArguments", generateRAIIHandleConstructorCallArguments( handle, constructorIt, false, {}, false ) },
                           { "constructorArguments", generateRAIIHandleConstructorArguments( handle, constructorIt, false, false ) },
                           { "constructorCall", constructorIt->first },
                           { "enter", enter },
                           { "handleConstructorArguments", handleConstructorArguments },
                           { "handleName", startLowerCase( handleType ) },
                           { "handleType", handleType },
                           { "leave", leave },
                           { "parentName", constructorIt->second.params.front().name },
                           { "successCheck", generateSuccessCheck( constructorIt->second.successCodes ) },
                           { "successCodePassToElement", successCodePassToElement },
                           { "vectorElementType", handleParamIt->type.type },
                           { "vectorName", startLowerCase( stripPrefix( handleParamIt->name, "p" ) ) },
                           { "vectorSize", vectorSize } } );
}

std::string
  VulkanHppGenerator::generateRAIIHandleConstructorVectorSingular( std::pair<std::string, HandleData> const &                             handle,
                                                                   std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                                   std::vector<ParamData>::const_iterator                                 handleParamIt,
                                                                   std::string const &                                                    enter,
                                                                   std::string const &                                                    leave ) const
{
  size_t                            returnParam    = static_cast<size_t>( std::distance( constructorIt->second.params.begin(), handleParamIt ) );
  std::map<size_t, VectorParamData> vectorParams   = determineVectorParams( constructorIt->second.params );
  std::set<size_t>                  singularParams = determineSingularParams( returnParam, vectorParams );

  std::string callArguments      = generateRAIIHandleConstructorCallArguments( handle, constructorIt, false, singularParams, true );
  std::string initializationList = generateRAIIHandleConstructorInitializationList( handle, constructorIt, handle.second.destructorIt, false );
  assert( !initializationList.empty() );
  std::string resultVariable = "VULKAN_HPP_NAMESPACE::Result result";
  std::string failureCheck   = generateFailureCheck( constructorIt->second.successCodes );
  std::string result         = "result";
  if ( isMultiSuccessCodeConstructor( handle.second.constructorIts ) )
  {
    resultVariable = "m_constructorSuccessCode";
    failureCheck   = std::regex_replace( failureCheck, std::regex( "result" ), "m_constructorSuccessCode" );
    result         = "m_constructorSuccessCode";
  }

  const std::string singularConstructorTemplate =
    R"(
${enter}    ${handleType}( ${constructorArguments} )
      : ${initializationList}, m_dispatcher( ${firstArgument}.getDispatcher() )
    {
      ${resultVariable} = static_cast<VULKAN_HPP_NAMESPACE::Result>( getDispatcher()->${constructorCall}( ${callArguments} ) );
      if ( ${failureCheck} )
      {
        throwResultException( ${result}, "${constructorCall}" );
      }
    }
${leave})";

  return replaceWithMap( singularConstructorTemplate,
                         { { "initializationList", initializationList },
                           { "callArguments", callArguments },
                           { "constructorArguments", generateRAIIHandleConstructorArguments( handle, constructorIt, true, false ) },
                           { "constructorCall", constructorIt->first },
                           { "enter", enter },
                           { "firstArgument", constructorIt->second.params[0].name },
                           { "failureCheck", failureCheck },
                           { "leave", leave },
                           { "handleType", stripPrefix( handle.first, "Vk" ) },
                           { "result", result },
                           { "resultVariable", resultVariable } } );
}

std::pair<std::string, std::string> VulkanHppGenerator::generateRAIIHandleConstructorVoid( std::pair<std::string, HandleData> const &         handle,
                                                                                           std::map<std::string, CommandData>::const_iterator constructorIt,
                                                                                           std::string const &                                enter,
                                                                                           std::string const &                                leave ) const
{
  assert( constructorIt->second.successCodes.empty() && constructorIt->second.errorCodes.empty() );
  std::vector<size_t> returnParams = determineReturnParams( constructorIt->second.params );
  if ( returnParams.size() == 1 )
  {
    assert( isHandleType( constructorIt->second.params[returnParams[0]].type.type ) );
    std::map<size_t, VectorParamData> vectorParams = determineVectorParams( constructorIt->second.params );
    if ( vectorParams.empty() )
    {
      return std::make_pair( generateRAIIHandleConstructorVoid1Return0Vector( handle, constructorIt, enter, leave ), "" );
    }
  }
  return std::make_pair( "", "" );
}

std::string
  VulkanHppGenerator::generateRAIIHandleConstructorVoid1Return0Vector( std::pair<std::string, HandleData> const &                             handle,
                                                                       std::map<std::string, VulkanHppGenerator::CommandData>::const_iterator constructorIt,
                                                                       std::string const &                                                    enter,
                                                                       std::string const &                                                    leave ) const
{
  std::string callArguments        = generateRAIIHandleConstructorCallArguments( handle, constructorIt, false, {}, true );
  std::string constructorArguments = generateRAIIHandleConstructorArguments( handle, constructorIt, false, false );
  std::string initializationList   = generateRAIIHandleConstructorInitializationList( handle, constructorIt, handle.second.destructorIt, false );
  if ( !initializationList.empty() )
  {
    initializationList += ", ";
  }

  const std::string constructorTemplate =
    R"(
${enter}    ${handleType}( ${constructorArguments} )
      : ${initializationList}m_dispatcher( ${firstArgument}.getDispatcher() )
    {
      getDispatcher()->${constructorCall}( ${callArguments} );
    }
${leave})";

  return replaceWithMap( constructorTemplate,
                         { { "callArguments", callArguments },
                           { "constructorArguments", constructorArguments },
                           { "constructorCall", constructorIt->first },
                           { "enter", enter },
                           { "firstArgument", constructorIt->second.params[0].name },
                           { "leave", leave },
                           { "handleType", stripPrefix( handle.first, "Vk" ) },
                           { "initializationList", initializationList } } );
}

std::string VulkanHppGenerator::generateRAIIHandleContext( std::pair<std::string, HandleData> const & handle,
                                                           std::set<std::string> const &              specialFunctions ) const
{
  const std::string contextTemplate = R"(
    class Context
    {
    public:
#if VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
      Context()
        : m_dispatcher( new VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::ContextDispatcher(
            m_dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>( "vkGetInstanceProcAddr" ) ) )
#else
      Context( PFN_vkGetInstanceProcAddr getInstanceProcAddr )
        : m_dispatcher( new VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::ContextDispatcher( getInstanceProcAddr ) )
#endif
      {}

      ~Context() = default;

      Context( Context const & ) = delete;
      Context( Context && rhs ) VULKAN_HPP_NOEXCEPT
#if VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
        : m_dynamicLoader( std::move( rhs.m_dynamicLoader ) )
        , m_dispatcher( rhs.m_dispatcher.release() )
#else
        : m_dispatcher( rhs.m_dispatcher.release() )
#endif
      {}
      Context & operator=( Context const & ) = delete;
      Context & operator=( Context && rhs ) VULKAN_HPP_NOEXCEPT
      {
        if ( this != &rhs )
        {
#if VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
          m_dynamicLoader = std::move( rhs.m_dynamicLoader );
#endif
          m_dispatcher.reset( rhs.m_dispatcher.release() );
        }
        return *this;
      }

      VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::ContextDispatcher const * getDispatcher() const
      {
        VULKAN_HPP_ASSERT( m_dispatcher->getVkHeaderVersion() == VK_HEADER_VERSION );
        return &*m_dispatcher;
      }

      void swap( VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Context & rhs )
      {
#if VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
        std::swap( m_dynamicLoader, rhs.m_dynamicLoader );
#endif
        m_dispatcher.swap( rhs.m_dispatcher );
      }

${memberFunctionDeclarations}

    private:
#if VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL
      VULKAN_HPP_NAMESPACE::DynamicLoader                                                 m_dynamicLoader;
#endif
      std::unique_ptr<VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::ContextDispatcher> m_dispatcher;
    };

)";

  return replaceWithMap( contextTemplate, { { "memberFunctionDeclarations", generateRAIIHandleCommandDeclarations( handle, specialFunctions ) } } );
}

std::string VulkanHppGenerator::generateRAIIHandleDestructorCallArguments( std::string const &                                handleType,
                                                                           std::map<std::string, CommandData>::const_iterator destructorIt ) const
{
  std::string arguments;
  bool        encounteredArgument = false;
  for ( auto param : destructorIt->second.params )
  {
    if ( encounteredArgument )
    {
      arguments += ", ";
    }
    if ( param.type.type == handleType )
    {
      std::string handleName = param.name;
      if ( param.type.isValue() )
      {
        arguments += "static_cast<" + handleType + ">( m_" + handleName + " )";
      }
      else
      {
        arguments += "reinterpret_cast<" + handleType + " const *>( &m_" + stripPluralS( startLowerCase( stripPrefix( handleName, "p" ) ) ) + " )";
      }
    }
    else if ( param.type.type == "VkAllocationCallbacks" )
    {
      // vk::AllocationCallbacks is stored as a member of the handle class
      arguments += "reinterpret_cast<const VkAllocationCallbacks *>( m_allocator )";
    }
    else if ( isHandleType( param.type.type ) )
    {
      assert( param.arraySizes.empty() );
      std::string argument = "m_" + param.name;
      if ( param.type.isValue() )
      {
        arguments += "static_cast<" + param.type.type + ">( " + argument + " )";
      }
      else
      {
        assert( param.type.isConstPointer() );
        assert( !param.lenExpression.empty() &&
                ( std::find_if( destructorIt->second.params.begin(),
                                destructorIt->second.params.end(),
                                [&param]( ParamData const & pd ) { return pd.name == param.lenExpression; } ) != destructorIt->second.params.end() ) );
        arguments += "reinterpret_cast<" + param.type.type + " const *>( &" + argument + " )";
      }
    }
    else
    {
      assert( ( param.type.type == "uint32_t" ) && param.type.isValue() && param.arraySizes.empty() && param.lenExpression.empty() && !param.optional );
      assert( std::find_if( destructorIt->second.params.begin(),
                            destructorIt->second.params.end(),
                            [&param]( ParamData const & pd ) { return pd.lenExpression == param.name; } ) != destructorIt->second.params.end() );
      arguments += "1";
    }
    encounteredArgument = true;
  }
  return arguments;
}

std::tuple<std::string, std::string, std::string, std::string, std::string, std::string, std::string>
  VulkanHppGenerator::generateRAIIHandleDetails( std::pair<std::string, HandleData> const & handle ) const
{
  std::string getConstructorSuccessCode;
  bool        multiSuccessCodeContructor = isMultiSuccessCodeConstructor( handle.second.constructorIts );
  if ( multiSuccessCodeContructor )
  {
    getConstructorSuccessCode = R"(
    VULKAN_HPP_NAMESPACE::Result getConstructorSuccessCode() const
    {
      return m_constructorSuccessCode;
    }
)";
  }

  auto [parentType, parentName] = getParentTypeAndName( handle );

  std::string handleName = generateRAIIHandleConstructorParamName( handle.first, handle.second.destructorIt );

  std::string clearMembers, moveConstructorInitializerList, moveAssignmentInstructions, memberVariables, swapMembers, releaseMembers;

  if ( handle.second.destructorIt != m_commands.end() )
  {
    moveAssignmentInstructions = "          clear();";

    clearMembers = "        if ( m_" + handleName + " )\n";
    clearMembers += "        {\n";
    clearMembers += "          getDispatcher()->" + handle.second.destructorIt->first + "( " +
                    generateRAIIHandleDestructorCallArguments( handle.first, handle.second.destructorIt ) + " );\n";
    clearMembers += "        }";
    for ( auto const & destructorParam : handle.second.destructorIt->second.params )
    {
      std::string memberName, memberType;
      if ( destructorParam.type.type == "Vk" + parentType )
      {
        memberName = parentName;
        memberType = "VULKAN_HPP_NAMESPACE::" + parentType;
      }
      else if ( destructorParam.type.type == handle.first )
      {
        memberName = handleName;
        memberType = generateNamespacedType( handle.first );
      }
      else if ( std::find_if( handle.second.destructorIt->second.params.begin(),
                              handle.second.destructorIt->second.params.end(),
                              [&destructorParam]( ParamData const & pd )
                              { return pd.lenExpression == destructorParam.name; } ) == handle.second.destructorIt->second.params.end() )
      {
        std::string name = destructorParam.name;
        if ( !destructorParam.type.isValue() )
        {
          name = startLowerCase( stripPrefix( name, "p" ) );
        }
        memberName = name;
        memberType = destructorParam.type.compose( "VULKAN_HPP_NAMESPACE" );
      }
      if ( !memberName.empty() )
      {
        clearMembers += "\n      m_" + memberName + " = nullptr;";
        moveConstructorInitializerList += "m_" + memberName + "( VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_" + memberName + ", {} ) ), ";
        moveAssignmentInstructions +=
          "\n          m_" + memberName + " = VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_" + memberName + ", {} );";
        memberVariables += "\n    " + memberType + " m_" + memberName + " = {};";
        swapMembers += "\n      std::swap( m_" + memberName + ", rhs.m_" + memberName + " );";
        if ( destructorParam.type.type != handle.first )
        {
          releaseMembers += "\n      m_" + memberName + " = nullptr;";
        }
      }
    }
  }
  else
  {
    if ( !handle.second.secondLevelCommands.empty() )
    {
      assert( !handle.second.constructorIts.empty() );
      assert( !handle.second.constructorIts.front()->second.params.empty() );
      auto const & frontType = handle.second.constructorIts.front()->second.params.front().type.type;
      assert( isHandleType( frontType ) );
#if !defined( NDEBUG )
      auto handleIt = m_handles.find( "Vk" + parentType );
#endif
      assert( handleIt != m_handles.end() );
      assert( handleIt->second.parent == frontType );
      std::string frontName = handle.second.constructorIts.front()->second.params.front().name;

      clearMembers += "\n        m_" + frontName + " = nullptr;";
      moveConstructorInitializerList = "m_" + frontName + "( VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_" + frontName + ", {} ) ), ";
      moveAssignmentInstructions =
        "\n          m_" + frontName + " = VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_" + frontName + ", {} );";
      memberVariables = "\n    VULKAN_HPP_NAMESPACE::" + stripPrefix( frontType, "Vk" ) + " m_" + frontName + " = {};";
      swapMembers     = "\n      std::swap( m_" + frontName + ", rhs.m_" + frontName + " );";
      releaseMembers += "\n        m_" + frontName + " = nullptr;";
    }
    clearMembers += "\n        m_" + handleName + " = nullptr;";
    moveConstructorInitializerList += "m_" + handleName + "( VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_" + handleName + ", {} ) ), ";
    moveAssignmentInstructions +=
      "\n          m_" + handleName + " = VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_" + handleName + ", {} );";
    memberVariables += "\n    " + generateNamespacedType( handle.first ) + " m_" + handleName + " = {};";
    swapMembers += "\n      std::swap( m_" + handleName + ", rhs.m_" + handleName + " );";
  }

  if ( multiSuccessCodeContructor )
  {
    clearMembers += "\n        m_constructorSuccessCode = VULKAN_HPP_NAMESPACE::Result::eErrorUnknown;";
    memberVariables += "\n    VULKAN_HPP_NAMESPACE::Result m_constructorSuccessCode = VULKAN_HPP_NAMESPACE::Result::eErrorUnknown;";
    swapMembers += "\n      std::swap( m_constructorSuccessCode, rhs.m_constructorSuccessCode );";
    moveConstructorInitializerList +=
      "m_constructorSuccessCode( VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_constructorSuccessCode, {} ) ), ";
    moveAssignmentInstructions +=
      "\n          m_constructorSuccessCode = VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_constructorSuccessCode, {} );";
    releaseMembers += "\n        m_constructorSuccessCode = VULKAN_HPP_NAMESPACE::Result::eErrorUnknown;";
  }

  if ( handle.first == "VkInstance" )
  {
    memberVariables += "\n      std::unique_ptr<VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::InstanceDispatcher> m_dispatcher;";
  }
  else if ( handle.first == "VkDevice" )
  {
    memberVariables += "\n      std::unique_ptr<VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::DeviceDispatcher> m_dispatcher;";
  }
  else if ( handle.second.constructorIts.front()->second.params.front().type.type == "VkDevice" )
  {
    memberVariables += "\n      VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::DeviceDispatcher const * m_dispatcher = nullptr;";
  }
  else
  {
    memberVariables += "\n      VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::InstanceDispatcher const * m_dispatcher = nullptr;";
  }
  clearMembers += "\n        m_dispatcher = nullptr;";
  swapMembers += "\n      std::swap( m_dispatcher, rhs.m_dispatcher );";
  releaseMembers += "\n        m_dispatcher = nullptr;";
  releaseMembers += "\n        return VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( m_" + handleName + ", nullptr );";

  if ( ( handle.first == "VkInstance" ) || ( handle.first == "VkDevice" ) )
  {
    moveConstructorInitializerList += "m_dispatcher( rhs.m_dispatcher.release() )";
    moveAssignmentInstructions += "\n        m_dispatcher.reset( rhs.m_dispatcher.release() );";
  }
  else
  {
    moveConstructorInitializerList += "m_dispatcher( VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_dispatcher, nullptr ) )";
    moveAssignmentInstructions += "\n        m_dispatcher = VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::exchange( rhs.m_dispatcher, nullptr );";
  }

  return std::make_tuple(
    clearMembers, getConstructorSuccessCode, memberVariables, moveConstructorInitializerList, moveAssignmentInstructions, swapMembers, releaseMembers );
}

std::string VulkanHppGenerator::generateRAIIHandleForwardDeclarations( std::vector<RequireData> const & requireData, std::string const & title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto handleIt = m_handles.find( type );
      if ( handleIt != m_handles.end() )
      {
        str += "  class " + stripPrefix( handleIt->first, "Vk" ) + ";\n";
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateRAIIHandles() const
{
  const std::string raiiHandlesTemplate = R"(
  //========================================
  //=== RAII HANDLE forward declarations ===
  //========================================

${forwardDeclarations}

  //====================
  //=== RAII HANDLES ===
  //====================

${raiiHandles}
)";

  std::string forwardDeclarations;
  for ( auto const & feature : m_features )
  {
    forwardDeclarations += generateRAIIHandleForwardDeclarations( feature.requireData, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    forwardDeclarations += generateRAIIHandleForwardDeclarations( extension.requireData, extension.name );
  }

  std::set<std::string> listedHandles;
  auto                  handleIt = m_handles.begin();
  assert( handleIt->first.empty() );
  std::string raiiHandles = generateRAIIHandleContext( *handleIt, m_RAIISpecialFunctions );
  for ( ++handleIt; handleIt != m_handles.end(); ++handleIt )
  {
    if ( isTypeUsed( handleIt->first ) )
    {
      raiiHandles += generateRAIIHandle( *handleIt, listedHandles, m_RAIISpecialFunctions );
    }
  }
  return replaceWithMap( raiiHandlesTemplate, { { "forwardDeclarations", forwardDeclarations }, { "raiiHandles", raiiHandles } } );
}

std::string VulkanHppGenerator::generateRAIIHandleSingularConstructorArguments( std::pair<std::string, HandleData> const &         handle,
                                                                                std::map<std::string, CommandData>::const_iterator constructorIt ) const
{
  std::string arguments = startLowerCase( stripPrefix( handle.first, "Vk" ) );
  if ( handle.second.destructorIt != m_commands.end() )
  {
    auto [parentType, parentName] = getParentTypeAndName( handle );
    parentType                    = "Vk" + parentType;
    for ( auto const & destructorParam : handle.second.destructorIt->second.params )
    {
      if ( ( destructorParam.type.type != parentType ) && ( destructorParam.type.type != handle.first ) &&
           ( std::find_if( handle.second.destructorIt->second.params.begin(),
                           handle.second.destructorIt->second.params.end(),
                           [&destructorParam]( ParamData const & pd )
                           { return pd.lenExpression == destructorParam.name; } ) == handle.second.destructorIt->second.params.end() ) )
      {
        if ( std::find_if( constructorIt->second.params.begin(),
                           constructorIt->second.params.end(),
                           [&destructorParam]( ParamData const & pd )
                           { return pd.type.type == destructorParam.type.type; } ) != constructorIt->second.params.end() )
        {
          if ( isHandleType( destructorParam.type.type ) )
          {
            assert( destructorParam.type.isValue() );
            arguments += ", static_cast<" + destructorParam.type.type + ">( *" + destructorParam.name + " )";
          }
          else
          {
            assert( destructorParam.type.type == "VkAllocationCallbacks" );
            arguments += ", allocator";
          }
        }
        else
        {
#if !defined( NDEBUG )
          bool found = false;
#endif
          for ( auto const & constructorParam : constructorIt->second.params )
          {
            auto structureIt = m_structs.find( constructorParam.type.type );
            if ( structureIt != m_structs.end() )
            {
              auto memberIt = findStructMemberItByType( destructorParam.type.type, structureIt->second.members );
              if ( memberIt != structureIt->second.members.end() )
              {
#if !defined( NDEBUG )
                found = true;
#endif
                assert( !constructorParam.type.isValue() );
                std::string argument = startLowerCase( stripPrefix( constructorParam.name, "p" ) ) + "." + memberIt->name;
                if ( isHandleType( memberIt->type.type ) )
                {
                  argument = "static_cast<" + memberIt->type.type + ">( " + argument + " )";
                }
                arguments += ", " + argument;
                break;
              }
            }
          }
          assert( found );
        }
      }
    }
  }
  return arguments;
}

std::string VulkanHppGenerator::generateRAIIHandleVectorSizeCheck( std::string const &                           name,
                                                                   CommandData const &                           commandData,
                                                                   size_t                                        initialSkipCount,
                                                                   std::map<size_t, std::vector<size_t>> const & countToVectorMap,
                                                                   std::set<size_t> const &                      skippedParams ) const
{
  std::string const throwTemplate =
    R"#(    if ( ${zeroSizeCheck}${firstVectorName}.size() != ${secondVectorName}.size() )
  {
    throw LogicError( VULKAN_HPP_NAMESPACE_STRING "::${className}::${commandName}: ${firstVectorName}.size() != ${secondVectorName}.size()" );
  })#";

  std::string const throwTemplateVoid =
    R"#(    if ( ${zeroSizeCheck}${firstVectorName}.size() * sizeof( ${firstDataType} ) != ${secondVectorName}.size() * sizeof( ${secondDataType} ) )
  {
    throw LogicError( VULKAN_HPP_NAMESPACE_STRING "::${className}::${commandName}: ${firstVectorName}.size() * sizeof( ${firstDataType} ) != ${secondVectorName}.size() * sizeof( ${secondDataType} )" );
  })#";

  std::string const throwTemplateSingle = R"#(    if ( ${vectorName}.size() != ${sizeValue} )
    {
      throw LogicError( VULKAN_HPP_NAMESPACE_STRING "::${className}::${commandName}: ${vectorName}.size() != ${sizeValue}" );
    })#";

  std::string className   = stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" );
  std::string commandName = generateCommandName( name, commandData.params, initialSkipCount );

  std::string sizeChecks;
  for ( auto const & cvm : countToVectorMap )
  {
    size_t      defaultStartIndex = determineDefaultStartIndex( commandData.params, skippedParams );
    std::string firstVectorName   = startLowerCase( stripPrefix( commandData.params[cvm.second[0]].name, "p" ) );

    if ( cvm.second.size() == 1 )
    {
      std::string sizeValue;
      if ( isLenByStructMember( commandData.params[cvm.second[0]].lenExpression, commandData.params[cvm.first] ) )
      {
        std::vector<std::string> lenParts = tokenize( commandData.params[cvm.second[0]].lenExpression, "->" );
        assert( lenParts.size() == 2 );
        sizeValue = startLowerCase( stripPrefix( lenParts[0], "p" ) ) + "." + lenParts[1];
      }
      else
      {
        assert( !commandData.params[cvm.second[0]].lenParams.empty() );
        if ( commandData.params[cvm.first].type.type == "VkSampleCountFlagBits" )
        {
          assert( commandData.params[cvm.second[0]].lenExpression == "(" + commandData.params[cvm.second[0]].lenParams[0].first + " + 31) / 32" );
          sizeValue = "( static_cast<uint32_t>( " + commandData.params[cvm.second[0]].lenParams[0].first + " ) + 31 ) / 32";
        }
        else
        {
          sizeValue = commandData.params[cvm.second[0]].lenExpression;
        }
      }
      sizeChecks += replaceWithMap(
        throwTemplateSingle, { { "className", className }, { "commandName", commandName }, { "sizeValue", sizeValue }, { "vectorName", firstVectorName } } );
    }
    else
    {
      for ( size_t i = 1; i < cvm.second.size(); i++ )
      {
        std::string secondVectorName  = startLowerCase( stripPrefix( commandData.params[cvm.second[i]].name, "p" ) );
        bool        withZeroSizeCheck = commandData.params[cvm.second[i]].optional && ( defaultStartIndex <= cvm.second[i] );
        if ( commandData.params[cvm.second[0]].type.type == "void" )
        {
          assert( commandData.params[cvm.second[i]].type.type == "void" );
          std::string firstDataType  = stripPrefix( commandData.params[cvm.second[0]].name, "p" ) + "Type";
          std::string secondDataType = stripPrefix( commandData.params[cvm.second[i]].name, "p" ) + "Type";
          sizeChecks += replaceWithMap( throwTemplateVoid,
                                        { { "firstDataType", firstDataType },
                                          { "firstVectorName", firstVectorName },
                                          { "className", className },
                                          { "commandName", commandName },
                                          { "secondDataType", secondDataType },
                                          { "secondVectorName", secondVectorName },
                                          { "zeroSizeCheck", withZeroSizeCheck ? ( "!" + secondVectorName + ".empty() && " ) : "" } } );
        }
        else
        {
          sizeChecks += replaceWithMap( throwTemplate,
                                        { { "firstVectorName", firstVectorName },
                                          { "className", className },
                                          { "commandName", commandName },
                                          { "secondVectorName", secondVectorName },
                                          { "zeroSizeCheck", withZeroSizeCheck ? ( "!" + secondVectorName + ".empty() && " ) : "" } } );
        }
        if ( i + 1 < cvm.second.size() )
        {
          sizeChecks += "\n";
        }
      }
    }
  }
  if ( !sizeChecks.empty() )
  {
    sizeChecks += "\n";
  }

  return sizeChecks;
}

std::string VulkanHppGenerator::generateResultAssignment( CommandData const & commandData ) const
{
  std::string resultAssignment;
  if ( ( commandData.returnType != "void" ) &&
       !( ( commandData.returnType == "VkResult" ) && ( commandData.successCodes.size() == 1 ) && commandData.errorCodes.empty() ) )
  {
    resultAssignment = commandData.returnType + " result = ";
  }
  return resultAssignment;
}

std::string VulkanHppGenerator::generateResultCheck(
  CommandData const & commandData, std::string const & className, std::string const & classSeparator, std::string commandName, bool enumerating ) const
{
  std::string resultCheck;
  if ( !commandData.errorCodes.empty() )
  {
    std::string successCodeList = generateSuccessCodeList( commandData.successCodes, enumerating );

    std::string const resultCheckTemplate =
      R"(resultCheck( static_cast<VULKAN_HPP_NAMESPACE::Result>( result ), VULKAN_HPP_NAMESPACE_STRING "::${className}${classSeparator}${commandName}"${successCodeList} );)";

    resultCheck = replaceWithMap(
      resultCheckTemplate,
      { { "className", className }, { "classSeparator", classSeparator }, { "commandName", commandName }, { "successCodeList", successCodeList } } );
  }
  return resultCheck;
}

// Intended only for `enum class Result`!
std::string VulkanHppGenerator::generateResultExceptions() const
{
  const std::string templateString = R"(
${enter}  class ${className} : public SystemError
  {
  public:
    ${className}( std::string const & message )
      : SystemError( make_error_code( ${enumName}::${enumMemberName} ), message ) {}
    ${className}( char const * message )
      : SystemError( make_error_code( ${enumName}::${enumMemberName} ), message ) {}
  };
${leave})";

  std::string str;
  auto        enumIt = m_enums.find( "VkResult" );
  for ( auto const & value : enumIt->second.values )
  {
    // Only generate exception class for non-alias results codes
    if ( value.alias.empty() && value.name.starts_with( "VK_ERROR" ) )
    {
      auto [enter, leave]   = generateProtection( value.protect );
      std::string valueName = generateEnumValueName( enumIt->first, value.name, false );
      str += replaceWithMap( templateString,
                             { { "className", stripPrefix( valueName, "eError" ) + "Error" },
                               { "enter", enter },
                               { "enumName", stripPrefix( enumIt->first, "Vk" ) },
                               { "enumMemberName", valueName },
                               { "leave", leave } } );
    }
  }
  return str;
}

std::string VulkanHppGenerator::generateReturnStatement( std::string const & commandName,
                                                         CommandData const & commandData,
                                                         std::string const & returnVariable,
                                                         std::string const & returnType,
                                                         std::string const & dataType,
                                                         size_t              initialSkipCount,
                                                         size_t              returnParam,
                                                         CommandFlavourFlags flavourFlags,
                                                         bool                enumerating,
                                                         bool                raii ) const
{
  bool unique = flavourFlags & CommandFlavourFlagBits::unique;

  std::string returnStatement;
  if ( commandData.returnType.starts_with( "Vk" ) )
  {
    if ( ( commandData.successCodes.size() == 1 ) || enumerating )
    {
      assert( commandData.successCodes[0] == "VK_SUCCESS" );
      if ( raii || commandData.errorCodes.empty() )
      {
        if ( !returnVariable.empty() )
        {
          returnStatement = "return " + returnVariable + ";";
        }
      }
      else
      {
        if ( returnVariable.empty() )
        {
          assert( !unique );
          returnStatement = "return createResultValueType( static_cast<VULKAN_HPP_NAMESPACE::Result>( result ) );";
        }
        else if ( unique )
        {
          assert( returnParam != INVALID_INDEX );
          returnStatement = "return createResultValueType( static_cast<VULKAN_HPP_NAMESPACE::Result>( result ), ";
          if ( dataType.starts_with( "std::" ) )
          {
            returnStatement += "std::move( unique" + startUpperCase( returnVariable ) + " )";
          }
          else
          {
            returnStatement += "UniqueHandle<" + dataType + ", Dispatch>( " + returnVariable + ", " +
                               generateObjectDeleter( commandName, commandData, initialSkipCount, returnParam ) + " )";
          }
          returnStatement += " );";
        }
        else
        {
          returnStatement = "return createResultValueType( static_cast<VULKAN_HPP_NAMESPACE::Result>( result ), " + returnVariable + " );";
        }
      }
    }
    else
    {
      if ( returnVariable.empty() )
      {
        assert( !unique );
        returnStatement = "return static_cast<VULKAN_HPP_NAMESPACE::" + stripPrefix( commandData.returnType, "Vk" ) + ">( result );";
      }
      else if ( unique )
      {
        assert( returnParam != INVALID_INDEX );
        assert( returnType.starts_with( "ResultValue<" ) && returnType.ends_with( ">" ) );
        returnStatement = "return " + returnType + "( static_cast<VULKAN_HPP_NAMESPACE::Result>( result ), ";
        if ( dataType.starts_with( "std::" ) )
        {
          returnStatement += "std::move( unique" + startUpperCase( returnVariable ) + " )";
        }
        else
        {
          returnStatement += "UniqueHandle<" + dataType + ", Dispatch>( " + returnVariable + ", " +
                             generateObjectDeleter( commandName, commandData, initialSkipCount, returnParam ) + " )";
        }
        returnStatement += " );";
      }
      else
      {
        assert( returnType.starts_with( raii ? "std::pair<VULKAN_HPP_NAMESPACE::Result, " : "ResultValue<" ) && returnType.ends_with( ">" ) );
        returnStatement =
          "return " + ( raii ? "std::make_pair" : returnType ) + "( static_cast<VULKAN_HPP_NAMESPACE::Result>( result ), " + returnVariable + " );";
      }
    }
  }
  else
  {
    assert( !unique );
    if ( returnVariable.empty() )
    {
      if ( commandData.returnType != "void" )
      {
        returnStatement = "return result;";
      }
    }
    else
    {
      returnStatement = "return " + returnVariable + ";";
    }
  }
  return returnStatement;
}

std::string VulkanHppGenerator::generateReturnType( CommandData const &                       commandData,
                                                    std::vector<size_t> const &               returnParams,
                                                    std::map<size_t, VectorParamData> const & vectorParams,
                                                    CommandFlavourFlags                       flavourFlags,
                                                    bool                                      raii,
                                                    std::string const &                       dataType ) const
{
  bool chained = flavourFlags & CommandFlavourFlagBits::chained;
  bool unique  = flavourFlags & CommandFlavourFlagBits::unique;

  std::string modifiedDataType = dataType;
  if ( chained )
  {
    assert( !unique );
    modifiedDataType = dataType.starts_with( "std::vector" )
                       ? ( std::string( "std::vector<StructureChain" ) + ( raii ? "" : ", StructureChainAllocator" ) + ">" )
                       : "StructureChain<X, Y, Z...>";
  }
  else if ( unique )
  {
    assert( !chained );
    assert( ( returnParams.size() == 1 ) && isHandleType( commandData.params[returnParams[0]].type.type ) );
    if ( dataType.starts_with( "std::vector" ) )
    {
      auto from = dataType.find( '<' ) + 1;
      assert( from != std::string::npos );
      auto to = dataType.find( '>', from );
      assert( to == dataType.length() - 1 );
      std::string type = dataType.substr( from, to - from );
      assert( type.starts_with( "VULKAN_HPP_NAMESPACE::" ) );
      modifiedDataType.replace( from, to - from, "UniqueHandle<" + type + ", Dispatch>, " + stripPrefix( type, "VULKAN_HPP_NAMESPACE::" ) + "Allocator" );
    }
    else
    {
      assert( !dataType.starts_with( "std::" ) );
      modifiedDataType = "UniqueHandle<" + dataType + ", Dispatch>";
    }
  }

  std::string returnType;
  if ( ( 1 < commandData.successCodes.size() ) && returnParams.empty() && !chained )
  {
    assert( ( commandData.returnType == "VkResult" ) && !unique );
    returnType = "VULKAN_HPP_NAMESPACE::Result";
  }
  else if ( ( commandData.returnType != "VkResult" ) && ( commandData.returnType != "void" ) )
  {
    assert( returnParams.empty() && !chained && !unique );
    if ( commandData.returnType.starts_with( "Vk" ) )
    {
      returnType = generateNamespacedType( commandData.returnType );
    }
    else
    {
      returnType = commandData.returnType;
    }
  }
  else if ( ( commandData.returnType == "void" ) ||
            ( ( commandData.returnType == "VkResult" ) && ( commandData.successCodes.size() == 1 ) && ( commandData.errorCodes.empty() || raii ) ) )
  {
    assert( !unique );
    assert( ( commandData.returnType != "void" ) || ( returnParams.size() <= 2 ) );
    returnType = modifiedDataType;
  }
  else
  {
    assert( commandData.returnType == "VkResult" );
    assert( !commandData.successCodes.empty() && ( commandData.successCodes[0] == "VK_SUCCESS" ) );
    if ( ( 1 < commandData.successCodes.size() ) && ( ( returnParams.size() == 1 ) || ( ( returnParams.size() == 2 ) && vectorParams.empty() ) ) )
    {
      assert( !commandData.errorCodes.empty() );
      returnType = ( raii ? "std::pair<VULKAN_HPP_NAMESPACE::Result, " : "ResultValue<" ) + modifiedDataType + ">";
    }
    else
    {
      assert(
        ( ( commandData.successCodes.size() == 1 ) || ( ( commandData.successCodes.size() == 2 ) && ( commandData.successCodes[1] == "VK_INCOMPLETE" ) ) ) &&
        ( returnParams.size() <= 3 ) );
      returnType = raii ? modifiedDataType : ( "typename ResultValueType<" + modifiedDataType + ">::type" );
    }
  }
  return returnType;
}

std::string VulkanHppGenerator::generateReturnVariable( CommandData const &                       commandData,
                                                        std::vector<size_t> const &               returnParams,
                                                        std::map<size_t, VectorParamData> const & vectorParams,
                                                        CommandFlavourFlags                       flavourFlags ) const
{
  bool chained  = flavourFlags & CommandFlavourFlagBits::chained;
  bool singular = flavourFlags & CommandFlavourFlagBits::singular;

  std::string returnVariable;
  switch ( returnParams.size() )
  {
    case 0: break;  // no return variable
    case 1:
      if ( chained )
      {
        returnVariable = "structureChain";
      }
      else
      {
        returnVariable = startLowerCase( stripPrefix( commandData.params[returnParams[0]].name, "p" ) );
        if ( singular )
        {
          returnVariable = stripPluralS( returnVariable );
        }
      }
      break;
    case 2:
      if ( chained )
      {
        returnVariable = "structureChains";
      }
      else
      {
        auto vectorParamIt = vectorParams.find( returnParams[1] );
        if ( vectorParamIt == vectorParams.end() )
        {  // the return variable is simply named "data", and holds the multi-return value stuff
          returnVariable = "data";
        }
        else
        {
          assert( vectorParamIt->second.lenParam == returnParams[0] );
          assert( !singular );
          returnVariable = startLowerCase( stripPrefix( commandData.params[returnParams[1]].name, "p" ) );
        }
      }
      break;
    case 3:
      assert( !chained && !singular );
      assert( ( ( vectorParams.size() == 1 ) && ( vectorParams.begin()->first == returnParams[2] ) &&
                ( vectorParams.begin()->second.lenParam == returnParams[1] ) ) ||
              ( ( vectorParams.size() == 2 ) && ( vectorParams.begin()->first == returnParams[1] ) &&
                ( vectorParams.begin()->second.lenParam == returnParams[0] ) && ( std::next( vectorParams.begin() )->first == returnParams[2] ) &&
                ( std::next( vectorParams.begin() )->second.lenParam == returnParams[0] ) ) );
      returnVariable = "data";
      break;
  }
  return returnVariable;
}

std::string VulkanHppGenerator::generateSizeCheck( std::vector<std::vector<MemberData>::const_iterator> const & arrayIts,
                                                   std::string const &                                          structName,
                                                   bool                                                         mutualExclusiveLens ) const
{
  std::string sizeCheck;
  if ( 1 < arrayIts.size() )
  {
    static const std::string throwTextTemplate = R"(      if ( ${throwCheck} )
      {
        throw LogicError( VULKAN_HPP_NAMESPACE_STRING"::${structName}::${structName}: ${throwCheck}" );
      }
)";

    std::string assertionText, throwText;
    if ( mutualExclusiveLens )
    {
      // exactly one of the arrays has to be non-empty
      std::string sum;
      for ( auto it : arrayIts )
      {
        sum += "!" + startLowerCase( stripPrefix( it->name, "p" ) ) + "_.empty() + ";
      }
      sum.resize( sum.size() - 3 );
      assertionText += "      VULKAN_HPP_ASSERT( ( " + sum + " ) <= 1);\n";
      throwText += replaceWithMap( throwTextTemplate, { { "structName", structName }, { "throwCheck", "1 < ( " + sum + " )" } } );
    }
    else
    {
      for ( size_t first = 0; first + 1 < arrayIts.size(); ++first )
      {
        assert( arrayIts[first]->name.starts_with( "p" ) );
        std::string firstName = startLowerCase( stripPrefix( arrayIts[first]->name, "p" ) ) + "_";
        for ( auto second = first + 1; second < arrayIts.size(); ++second )
        {
          assert( arrayIts[second]->name.starts_with( "p" ) );
          std::string secondName     = startLowerCase( stripPrefix( arrayIts[second]->name, "p" ) ) + "_";
          std::string assertionCheck = firstName + ".size() == " + secondName + ".size()";
          std::string throwCheck     = firstName + ".size() != " + secondName + ".size()";
          if ( ( !arrayIts[first]->optional.empty() && arrayIts[first]->optional.front() ) ||
               ( !arrayIts[second]->optional.empty() && arrayIts[second]->optional.front() ) )
          {
            assertionCheck = "( " + assertionCheck + " )";
            throwCheck     = "( " + throwCheck + " )";
            if ( !arrayIts[second]->optional.empty() && arrayIts[second]->optional.front() )
            {
              assertionCheck = secondName + ".empty() || " + assertionCheck;
              throwCheck     = "!" + secondName + ".empty() && " + throwCheck;
            }
            if ( !arrayIts[first]->optional.empty() && arrayIts[first]->optional.front() )
            {
              assertionCheck = firstName + ".empty() || " + assertionCheck;
              throwCheck     = "!" + firstName + ".empty() && " + throwCheck;
            }
          }
          assertionText += "      VULKAN_HPP_ASSERT( " + assertionCheck + " );\n";
          throwText += replaceWithMap( throwTextTemplate, { { "structName", structName }, { "throwCheck", throwCheck } } );
        }
      }
    }
    sizeCheck += "\n#ifdef VULKAN_HPP_NO_EXCEPTIONS\n" + assertionText + "#else\n" + throwText + "#endif /*VULKAN_HPP_NO_EXCEPTIONS*/\n" + "    ";
  }
  return sizeCheck;
}

std::string VulkanHppGenerator::generateStaticAssertions() const
{
  std::string           staticAssertions;
  std::set<std::string> listedStructs;
  for ( auto const & feature : m_features )
  {
    staticAssertions += generateStaticAssertions( feature.requireData, feature.name, listedStructs );
  }
  for ( auto const & extension : m_extensions )
  {
    staticAssertions += generateStaticAssertions( extension.requireData, extension.name, listedStructs );
  }
  return staticAssertions;
}

std::string VulkanHppGenerator::generateStaticAssertions( std::vector<RequireData> const & requireData,
                                                          std::string const &              title,
                                                          std::set<std::string> &          listedStructs ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto handleIt = m_handles.find( type );
      if ( handleIt != m_handles.end() )
      {
        std::string const assertionTemplate = R"(
VULKAN_HPP_STATIC_ASSERT( sizeof( VULKAN_HPP_NAMESPACE::${className} ) == sizeof( Vk${className} ), "handle and wrapper have different size!" );
VULKAN_HPP_STATIC_ASSERT( std::is_nothrow_move_constructible<VULKAN_HPP_NAMESPACE::${className}>::value, "${className} is not nothrow_move_constructible!" );
)";
        str += replaceWithMap( assertionTemplate, { { "className", stripPrefix( handleIt->first, "Vk" ) } } );
      }
      else
      {
        auto structIt = m_structs.find( type );
        if ( ( structIt != m_structs.end() ) && listedStructs.insert( type ).second )
        {
          std::string const assertionTemplate = R"(
VULKAN_HPP_STATIC_ASSERT( sizeof( VULKAN_HPP_NAMESPACE::${structureType} ) == sizeof( Vk${structureType} ), "struct and wrapper have different size!" );
VULKAN_HPP_STATIC_ASSERT( std::is_standard_layout<VULKAN_HPP_NAMESPACE::${structureType}>::value, "struct wrapper is not a standard layout!" );
VULKAN_HPP_STATIC_ASSERT( std::is_nothrow_move_constructible<VULKAN_HPP_NAMESPACE::${structureType}>::value, "${structureType} is not nothrow_move_constructible!" );
)";
          str += replaceWithMap( assertionTemplate, { { "structureType", stripPrefix( structIt->first, "Vk" ) } } );
        }
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateStruct( std::pair<std::string, StructureData> const & structure, std::set<std::string> & listedStructs ) const
{
  assert( listedStructs.find( structure.first ) == listedStructs.end() );

  std::string str;
  for ( auto const & member : structure.second.members )
  {
    auto structIt = m_structs.find( member.type.type );
    if ( ( structIt != m_structs.end() ) && ( structure.first != member.type.type ) && ( listedStructs.find( member.type.type ) == listedStructs.end() ) )
    {
      str += generateStruct( *structIt, listedStructs );
    }
  }

  if ( !structure.second.subStruct.empty() )
  {
    auto structureIt = m_structs.find( structure.second.subStruct );
    if ( ( structureIt != m_structs.end() ) && ( listedStructs.find( structureIt->first ) == listedStructs.end() ) )
    {
      str += generateStruct( *structureIt, listedStructs );
    }
  }

  if ( structure.second.isUnion )
  {
    str += generateUnion( structure );
  }
  else
  {
    str += generateStructure( structure );
  }

  listedStructs.insert( structure.first );
  return str;
}

std::string VulkanHppGenerator::generateStructCompareOperators( std::pair<std::string, StructureData> const & structData ) const
{
  static const std::set<std::string> simpleTypes = { "char",   "double",  "DWORD",    "float",    "HANDLE",  "HINSTANCE", "HMONITOR",
                                                     "HWND",   "int",     "int8_t",   "int16_t",  "int32_t", "int64_t",   "LPCWSTR",
                                                     "size_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t" };
  // two structs are compared by comparing each of the elements
  std::string compareMembers, spaceshipMembers;
  std::string intro             = "";
  bool        nonDefaultCompare = false;
  std::string spaceshipOrdering = containsFloatingPoints( structData.second.members ) ? "std::partial_ordering" : "std::strong_ordering";
  for ( size_t i = 0; i < structData.second.members.size(); i++ )
  {
    MemberData const & member = structData.second.members[i];
    auto               typeIt = m_types.find( member.type.type );
    assert( typeIt != m_types.end() );
    if ( ( typeIt->second.category == TypeCategory::ExternalType ) && member.type.postfix.empty() &&
         ( simpleTypes.find( member.type.type ) == simpleTypes.end() ) )
    {
      nonDefaultCompare = true;
      // this type might support operator==() or operator<=>()... that is, use memcmp
      compareMembers += intro + "( memcmp( &" + member.name + ", &rhs." + member.name + ", sizeof( " + member.type.type + " ) ) == 0 )";

      static const std::string spaceshipMemberTemplate =
        R"(      if ( auto cmp = memcmp( &${name}, &rhs.${name}, sizeof( ${type} ) ); cmp != 0 )
        return ( cmp < 0 ) ? ${ordering}::less : ${ordering}::greater;
)";
      spaceshipMembers +=
        replaceWithMap( spaceshipMemberTemplate, { { "name", member.name }, { "ordering", spaceshipOrdering }, { "type", member.type.type } } );
    }
    else if ( member.type.type == "char" && !member.lenExpressions.empty() )
    {
      // compare null-terminated strings
      nonDefaultCompare = true;
      assert( member.lenExpressions.size() < 3 );
      if ( member.lenExpressions.size() == 1 )
      {
        assert( member.lenExpressions[0] == "null-terminated" );
        compareMembers += intro + "( ( " + member.name + " == rhs." + member.name + " ) || ( strcmp( " + member.name + ", rhs." + member.name + " ) == 0 ) )";

        static const std::string spaceshipMemberTemplate =
          R"(     if ( ${name} != rhs.${name} )
        if ( auto cmp = strcmp( ${name}, rhs.${name} ); cmp != 0 )
          return ( cmp < 0 ) ? ${ordering}::less : ${ordering}::greater;
)";
        spaceshipMembers += replaceWithMap( spaceshipMemberTemplate, { { "name", member.name }, { "ordering", spaceshipOrdering } } );
      }
      else
      {
        assert( member.lenExpressions[1] == "null-terminated" );
        assert( ( member.type.prefix == "const" ) && ( member.type.postfix == "* const *" ) );
        static const std::string compareMemberTemplate =
          R"(std::equal( ${name}, ${name} + ${count}, rhs.${name}, []( char const * left, char const * right ) { return ( left == right ) || ( strcmp( left, right ) == 0 ); } ))";
        compareMembers += intro + replaceWithMap( compareMemberTemplate, { { "count", member.lenExpressions[0] }, { "name", member.name } } );

        static const std::string spaceshipMemberTemplate = R"(      for ( size_t i = 0; i < ${count}; ++i )
      {
        if ( ${name}[i] != rhs.${name}[i] )
          if ( auto cmp = strcmp( ${name}[i], rhs.${name}[i] ); cmp != 0 )
            return cmp < 0 ? ${ordering}::less : ${ordering}::greater;
      }
)";
        spaceshipMembers +=
          replaceWithMap( spaceshipMemberTemplate, { { "count", member.lenExpressions[0] }, { "name", member.name }, { "ordering", spaceshipOrdering } } );
      }
    }
    else
    {
      // for all others, we use the operator== of that type
      compareMembers += intro + "( " + member.name + " == rhs." + member.name + " )";
      spaceshipMembers += "      if ( auto cmp = " + member.name + " <=> rhs." + member.name + "; cmp != 0 ) return cmp;\n";
    }
    intro = "\n          && ";
  }

  std::string structName = stripPrefix( structData.first, "Vk" );

  std::string compareBody, spaceshipOperator, spaceshipOperatorIf, spaceshipOperatorElse, spaceshipOperatorEndif;
  if ( nonDefaultCompare )
  {
    compareBody = "      return " + compareMembers + ";";

    if ( !containsFuncPointer( structData.first ) )
    {
      static const std::string spaceshipOperatorTemplate =
        R"(    ${ordering} operator<=>( ${name} const & rhs ) const VULKAN_HPP_NOEXCEPT
    {
${spaceshipMembers}
      return ${ordering}::equivalent;
    })";
      spaceshipOperatorIf = "#if defined(VULKAN_HPP_HAS_SPACESHIP_OPERATOR)";
      spaceshipOperator =
        replaceWithMap( spaceshipOperatorTemplate, { { "name", structName }, { "ordering", spaceshipOrdering }, { "spaceshipMembers", spaceshipMembers } } );
      spaceshipOperatorElse  = "#endif\n";
      spaceshipOperatorEndif = "";
    }
  }
  else
  {
    // use reflection only if VULKAN_HPP_USE_REFLECT is defined
    static const std::string compareBodyTemplate = R"(#if defined( VULKAN_HPP_USE_REFLECT )
      return this->reflect() == rhs.reflect();
#else
      return ${compareMembers};
#endif)";
    compareBody                                  = replaceWithMap( compareBodyTemplate, { { "compareMembers", compareMembers } } );

    if ( !containsFuncPointer( structData.first ) )
    {
      spaceshipOperatorIf    = "#if defined(VULKAN_HPP_HAS_SPACESHIP_OPERATOR)";
      spaceshipOperator      = "auto operator<=>( " + structName + " const & ) const = default;";
      spaceshipOperatorElse  = "#else";
      spaceshipOperatorEndif = "#endif\n";
    }
  }

  static const std::string compareTemplate = R"(
${spaceshipOperatorIf}
${spaceshipOperator}
${spaceshipOperatorElse}
    bool operator==( ${name} const & rhs ) const VULKAN_HPP_NOEXCEPT
    {
${compareBody}
    }

    bool operator!=( ${name} const & rhs ) const VULKAN_HPP_NOEXCEPT
    {
      return !operator==( rhs );
    }
${spaceshipOperatorEndif})";

  return replaceWithMap( compareTemplate,
                         { { "name", structName },
                           { "compareBody", compareBody },
                           { "spaceshipOperator", spaceshipOperator },
                           { "spaceshipOperatorElse", spaceshipOperatorElse },
                           { "spaceshipOperatorEndif", spaceshipOperatorEndif },
                           { "spaceshipOperatorIf", spaceshipOperatorIf } } );
}

std::string VulkanHppGenerator::generateStructConstructors( std::pair<std::string, StructureData> const & structData ) const
{
  // the constructor with all the elements as arguments, with defaults
  // and the simple copy constructor from the corresponding vulkan structure
  static const std::string constructors = R"(${constexpr}${structName}(${arguments}) VULKAN_HPP_NOEXCEPT
    ${initializers}
    {}

    ${constexpr}${structName}( ${structName} const & rhs ) VULKAN_HPP_NOEXCEPT = default;

    ${structName}( Vk${structName} const & rhs ) VULKAN_HPP_NOEXCEPT
      : ${structName}( *reinterpret_cast<${structName} const *>( &rhs ) )
    {}
)";

  std::string arguments, initializers;
  bool        listedArgument = false;
  bool        firstArgument  = true;
  for ( auto const & member : structData.second.members )
  {
    // gather the arguments
    std::string argument = generateStructConstructorArgument( listedArgument, member, true );
    if ( !argument.empty() )
    {
      listedArgument = true;
      arguments += argument;
    }

    // gather the initializers; skip members with exactly one legal value
    if ( member.value.empty() )
    {
      initializers += std::string( firstArgument ? ": " : ", " ) + member.name + "( " + member.name + "_ )";
      firstArgument = false;
    }
  }
  auto pNextIt = std::find_if( structData.second.members.begin(), structData.second.members.end(), []( MemberData const & md ) { return md.name == "pNext"; } );
  if ( pNextIt != structData.second.members.end() )
  {
    // add pNext as a last optional argument to the constructor
    arguments += std::string( listedArgument ? ", " : "" ) + pNextIt->type.compose( "VULKAN_HPP_NAMESPACE" ) + " pNext_ = nullptr";
  }

  std::string str = replaceWithMap( constructors,
                                    { { "arguments", arguments },
                                      { "constexpr", generateConstexprString( structData.first ) },
                                      { "initializers", initializers },
                                      { "structName", stripPrefix( structData.first, "Vk" ) } } );

  str += generateStructConstructorsEnhanced( structData );
  return str;
}

std::string VulkanHppGenerator::generateStructConstructorsEnhanced( std::pair<std::string, StructureData> const & structData ) const
{
  if ( std::find_if( structData.second.members.begin(), structData.second.members.end(), [this]( MemberData const & md ) { return hasLen( md ); } ) !=
       structData.second.members.end() )
  {
    // map from len-members to all the array members using that len
    std::map<std::vector<MemberData>::const_iterator, std::vector<std::vector<MemberData>::const_iterator>> lenIts;
    for ( auto mit = structData.second.members.begin(); mit != structData.second.members.end(); ++mit )
    {
      if ( hasLen( *mit ) )
      {
        std::string lenName = ( mit->lenExpressions.front() == "codeSize / 4" ) ? "codeSize" : mit->lenExpressions.front();
        auto        lenIt   = findStructMemberIt( lenName, structData.second.members );
        assert( lenIt != mit );
        lenIts[lenIt].push_back( mit );
      }
    }

    std::string arguments, initializers;
    bool        listedArgument = false;
    bool        firstArgument  = true;
    bool        arrayListed    = false;
    std::string templateHeader, sizeChecks;
    for ( auto mit = structData.second.members.begin(); mit != structData.second.members.end(); ++mit )
    {
      // gather the initializers
      if ( mit->name == "pNext" )  // for pNext, we just get the initializer... the argument is added at the end
      {
        initializers += std::string( firstArgument ? ":" : "," ) + " pNext( pNext_ )";
        firstArgument = false;
      }
      else if ( mit->value.empty() )  // skip constant members
      {
        auto litit = lenIts.find( mit );
        if ( litit != lenIts.end() )
        {
          // len arguments just have an initalizer, from the ArrayProxyNoTemporaries size
          initializers +=
            ( firstArgument ? ": " : ", " ) + mit->name + "( " + generateLenInitializer( mit, litit, structData.second.mutualExclusiveLens ) + " )";
          sizeChecks += generateSizeCheck( litit->second, stripPrefix( structData.first, "Vk" ), structData.second.mutualExclusiveLens );
        }
        else if ( hasLen( *mit ) )
        {
          assert( mit->name.starts_with( "p" ) );
          std::string argumentName = startLowerCase( stripPrefix( mit->name, "p" ) ) + "_";

          assert( mit->type.postfix.ends_with( "*" ) );
          std::string argumentType = trimEnd( stripPostfix( mit->type.compose( "VULKAN_HPP_NAMESPACE" ), "*" ) );
          if ( ( mit->type.type == "void" ) && ( argumentType.find( '*' ) == std::string::npos ) )
          {
            // the argument after stripping one pointer is just void
            assert( templateHeader.empty() );
            templateHeader = "    template <typename T>\n";

            size_t pos = argumentType.find( "void" );
            assert( pos != std::string::npos );
            argumentType.replace( pos, strlen( "void" ), "T" );
          }

          arguments += listedArgument ? ", " : "";
          arguments += "VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<" + argumentType + "> const & " + argumentName;
          if ( arrayListed )
          {
            arguments += " = {}";
          }
          listedArgument = true;
          arrayListed    = true;

          initializers += ( firstArgument ? ": " : ", " ) + mit->name + "( " + argumentName + ".data() )";
        }
        else
        {
          std::string argument = generateStructConstructorArgument( listedArgument, *mit, arrayListed );
          if ( !argument.empty() )
          {
            listedArgument = true;
            arguments += argument;
          }
          initializers += ( firstArgument ? ": " : ", " ) + mit->name + "( " + mit->name + "_ )";
        }
        firstArgument = false;
      }
    }

    auto pNextIt =
      std::find_if( structData.second.members.begin(), structData.second.members.end(), []( MemberData const & md ) { return md.name == "pNext"; } );
    if ( pNextIt != structData.second.members.end() )
    {
      // add pNext as a last optional argument to the constructor
      arguments += std::string( listedArgument ? ", " : "" ) + pNextIt->type.compose( "VULKAN_HPP_NAMESPACE" ) + " pNext_ = nullptr";
    }

    static const std::string constructorTemplate = R"(
#if !defined( VULKAN_HPP_DISABLE_ENHANCED_MODE )
${templateHeader}    ${structName}( ${arguments} )
    ${initializers}
    {${sizeChecks}}
#endif /*VULKAN_HPP_DISABLE_ENHANCED_MODE*/
)";

    return replaceWithMap( constructorTemplate,
                           { { "arguments", arguments },
                             { "initializers", initializers },
                             { "sizeChecks", sizeChecks },
                             { "structName", stripPrefix( structData.first, "Vk" ) },
                             { "templateHeader", templateHeader } } );
  }
  return "";
}

std::string VulkanHppGenerator::generateStructConstructorArgument( bool listedArgument, MemberData const & memberData, bool withDefault ) const
{
  // skip members 'pNext' and members with a specified value, as they are never explicitly set
  std::string str;
  if ( ( memberData.name != "pNext" ) && memberData.value.empty() )
  {
    str += ( listedArgument ? ( ", " ) : "" );
    if ( memberData.arraySizes.empty() )
    {
      str += memberData.type.compose( "VULKAN_HPP_NAMESPACE" ) + " ";
    }
    else
    {
      str += generateStandardArray( memberData.type.compose( "VULKAN_HPP_NAMESPACE" ), memberData.arraySizes ) + " const & ";
    }
    str += memberData.name + "_";

    if ( withDefault )
    {
      str += " = ";
      auto enumIt = m_enums.find( memberData.type.type );
      if ( enumIt != m_enums.end() && memberData.type.postfix.empty() )
      {
        str += generateEnumInitializer( memberData.type, memberData.arraySizes, enumIt->second.values, enumIt->second.isBitmask );
      }
      else
      {
        assert( memberData.value.empty() );
        // all the rest can be initialized with just {}
        str += "{}";
      }
    }
  }
  return str;
}

std::string VulkanHppGenerator::generateStructHashStructure( std::pair<std::string, StructureData> const & structure,
                                                             std::set<std::string> &                       listedStructs ) const
{
  assert( listedStructs.find( structure.first ) == listedStructs.end() );

  std::string str;
  for ( auto const & member : structure.second.members )
  {
    auto structIt = m_structs.find( member.type.type );
    if ( ( structIt != m_structs.end() ) && ( structure.first != member.type.type ) && ( listedStructs.find( member.type.type ) == listedStructs.end() ) )
    {
      str += generateStructHashStructure( *structIt, listedStructs );
    }
  }

  if ( !containsUnion( structure.first ) )
  {
    static const std::string hashTemplate = R"(
  ${enter}template <> struct hash<VULKAN_HPP_NAMESPACE::${structureType}>
  {
    std::size_t operator()(VULKAN_HPP_NAMESPACE::${structureType} const & ${structureName}) const VULKAN_HPP_NOEXCEPT
    {
      std::size_t seed = 0;
${hashSum}
      return seed;
    }
  };
${leave})";

    auto [enter, leave] = generateProtection( getProtectFromType( structure.first ) );

    std::string structureType = stripPrefix( structure.first, "Vk" );
    std::string structureName = startLowerCase( structureType );
    str += replaceWithMap( hashTemplate,
                           { { "enter", enter },
                             { "hashSum", generateStructHashSum( structureName, structure.second.members ) },
                             { "leave", leave },
                             { "structureName", structureName },
                             { "structureType", structureType } } );
  }

  listedStructs.insert( structure.first );
  return str;
}

std::string VulkanHppGenerator::generateStructHashStructures() const
{
  const std::string hashesTemplate = R"(
#if 14 <= VULKAN_HPP_CPP_VERSION
  //======================================
  //=== HASH structures for structures ===
  //======================================

#  if !defined( VULKAN_HPP_HASH_COMBINE )
#    define VULKAN_HPP_HASH_COMBINE( seed, value ) \
      seed ^= std::hash<std::decay<decltype( value )>::type>{}( value ) + 0x9e3779b9 + ( seed << 6 ) + ( seed >> 2 )
#  endif

${hashes}
#endif    // 14 <= VULKAN_HPP_CPP_VERSION
)";

  // Note reordering structs or handles by features and extensions is not possible!
  std::set<std::string> listedStructs;
  std::string           hashes;
  for ( auto const & structure : m_structs )
  {
    if ( ( listedStructs.find( structure.first ) == listedStructs.end() ) && isTypeUsed( structure.first ) )
    {
      hashes += generateStructHashStructure( structure, listedStructs );
    }
  }
  return replaceWithMap( hashesTemplate, { { "hashes", hashes } } );
}

std::string VulkanHppGenerator::generateStructHashSum( std::string const & structName, std::vector<MemberData> const & members ) const
{
  std::string hashSum;
  for ( auto const & member : members )
  {
    if ( !member.arraySizes.empty() )
    {
      assert( member.arraySizes.size() < 3 );
      hashSum += "    for ( size_t i = 0; i < " + member.arraySizes[0] + "; ++i )\n";
      hashSum += "    {\n";
      if ( member.arraySizes.size() == 1 )
      {
        hashSum += "      VULKAN_HPP_HASH_COMBINE( seed, " + structName + "." + member.name + "[i] );\n";
      }
      else
      {
        hashSum += "      for ( size_t j=0; j < " + member.arraySizes[1] + "; ++j )\n";
        hashSum += "      {\n";
        hashSum += "        VULKAN_HPP_HASH_COMBINE( seed, " + structName + "." + member.name + "[i][j] );\n";
        hashSum += "      }\n";
      }
      hashSum += "    }\n";
    }
    else if ( member.type.type == "char" && !member.lenExpressions.empty() )
    {
      assert( member.lenExpressions.size() < 3 );
      if ( member.lenExpressions.size() == 1 )
      {
        assert( member.lenExpressions[0] == "null-terminated" );
        hashSum += "    for ( const char* p = " + structName + "." + member.name + "; *p != '\\0'; ++p )\n";
        hashSum += "    {\n";
        hashSum += "      VULKAN_HPP_HASH_COMBINE( seed, *p );\n";
        hashSum += "    }\n";
      }
      else
      {
        assert( member.lenExpressions[1] == "null-terminated" );
        hashSum += "    for ( size_t i = 0; i < " + structName + "." + member.lenExpressions[0] + "; ++i )\n";
        hashSum += "    {\n";
        hashSum += "        for ( const char* p = " + structName + "." + member.name + "[i]; *p != '\\0'; ++p )\n";
        hashSum += "        {\n";
        hashSum += "          VULKAN_HPP_HASH_COMBINE( seed, *p );\n";
        hashSum += "        }\n";
        hashSum += "    }\n";
      }
    }
    else
    {
      hashSum += "    VULKAN_HPP_HASH_COMBINE( seed, " + structName + "." + member.name + " );\n";
    }
  }
  assert( !hashSum.empty() );
  return hashSum.substr( 0, hashSum.size() - 1 );
}

std::string VulkanHppGenerator::generateStructs() const
{
  const std::string structsTemplate = R"(
  //===============
  //=== STRUCTS ===
  //===============

${structs}
)";

  // Note reordering structs or handles by features and extensions is not possible!
  std::set<std::string> listedStructs;
  std::string           structs;
  for ( auto const & structure : m_structs )
  {
    if ( ( listedStructs.find( structure.first ) == listedStructs.end() ) && isTypeUsed( structure.first ) )
    {
      structs += generateStruct( structure, listedStructs );
    }
  }
  return replaceWithMap( structsTemplate, { { "structs", structs } } );
}

std::string VulkanHppGenerator::generateStructure( std::pair<std::string, StructureData> const & structure ) const
{
  auto [enter, leave] = generateProtection( getProtectFromType( structure.first ) );

  std::string str = "\n" + enter;

  static const std::string constructorsTemplate = R"(
#if !defined( VULKAN_HPP_NO_STRUCT_CONSTRUCTORS )
${constructors}
${subConstructors}
    ${structName} & operator=( ${structName} const & rhs ) VULKAN_HPP_NOEXCEPT = default;
#endif /*VULKAN_HPP_NO_STRUCT_CONSTRUCTORS*/

    ${structName} & operator=( Vk${structName} const & rhs ) VULKAN_HPP_NOEXCEPT
    {
      *this = *reinterpret_cast<VULKAN_HPP_NAMESPACE::${structName} const *>( &rhs );
      return *this;
    }
)";

  std::string constructorsAndSetters = replaceWithMap( constructorsTemplate,
                                                       { { "constructors", generateStructConstructors( structure ) },
                                                         { "structName", stripPrefix( structure.first, "Vk" ) },
                                                         { "subConstructors", generateStructSubConstructor( structure ) } } );

  if ( !structure.second.returnedOnly )
  {
    // only structs that are not returnedOnly get setters!
    constructorsAndSetters += "\n#if !defined( VULKAN_HPP_NO_STRUCT_SETTERS )";
    for ( size_t i = 0; i < structure.second.members.size(); i++ )
    {
      constructorsAndSetters += generateStructSetter( stripPrefix( structure.first, "Vk" ), structure.second.members, i );
    }
    constructorsAndSetters += "#endif /*VULKAN_HPP_NO_STRUCT_SETTERS*/\n";
  }

  std::string structureType = stripPrefix( structure.first, "Vk" );
  // the member variables
  std::string members, memberNames, memberTypes, sTypeValue;
  std::tie( members, memberNames, memberTypes, sTypeValue ) = generateStructMembers( structure );

  // reflect is meaningfull for structs only, filter out unions
  std::string reflect;
  if ( !structure.second.isUnion )
  {
    // use reflection only if VULKAN_HPP_USE_REFLECT is defined
    static const std::string reflectTemplate = R"(
#if defined( VULKAN_HPP_USE_REFLECT )
#if 14 <= VULKAN_HPP_CPP_VERSION
    auto
#else
    std::tuple<${memberTypes}>
#endif
      reflect() const VULKAN_HPP_NOEXCEPT
    {
      return std::tie( ${memberNames} );
    }
#endif
)";

    reflect = replaceWithMap( reflectTemplate, { { "memberNames", memberNames }, { "memberTypes", memberTypes } } );
  }

  // operator==() and operator!=()
  // only structs without a union as a member can have a meaningfull == and != operation; we filter them out
  std::string compareOperators;
  if ( !containsUnion( structure.first ) )
  {
    compareOperators += generateStructCompareOperators( structure );
  }

  static const std::string structureTemplate = R"(  struct ${structureType}
  {
    using NativeType = Vk${structureType};

${allowDuplicate}
${typeValue}
${constructorsAndSetters}

    operator Vk${structureType} const &() const VULKAN_HPP_NOEXCEPT
    {
      return *reinterpret_cast<const Vk${structureType}*>( this );
    }

    operator Vk${structureType} &() VULKAN_HPP_NOEXCEPT
    {
      return *reinterpret_cast<Vk${structureType}*>( this );
    }
${reflect}
${compareOperators}
    public:
${members}
  };
)";

  std::string allowDuplicate, typeValue;
  if ( !sTypeValue.empty() )
  {
    allowDuplicate = std::string( "    static const bool allowDuplicate = " ) + ( structure.second.allowDuplicate ? "true;" : "false;" );
    typeValue      = "    static VULKAN_HPP_CONST_OR_CONSTEXPR StructureType structureType = StructureType::" + sTypeValue + ";\n";
  }
  str += replaceWithMap( structureTemplate,
                         { { "allowDuplicate", allowDuplicate },
                           { "constructorsAndSetters", constructorsAndSetters },
                           { "compareOperators", compareOperators },
                           { "members", members },
                           { "reflect", reflect },
                           { "structureType", structureType },
                           { "typeValue", typeValue } } );

  if ( !sTypeValue.empty() )
  {
    std::string cppTypeTemplate = R"(
  template <>
  struct CppType<StructureType, StructureType::${sTypeValue}>
  {
    using Type = ${structureType};
  };
)";
    str += replaceWithMap( cppTypeTemplate, { { "sTypeValue", sTypeValue }, { "structureType", structureType } } );
  }

  for ( auto const & alias : m_structAliases )
  {
    if ( alias.second.name == structure.first )
    {
      str += "  using " + stripPrefix( alias.first, "Vk" ) + " = " + structureType + ";\n";
    }
  }

  str += leave;
  return str;
}

std::string VulkanHppGenerator::generateStructExtendsStructs() const
{
  const std::string structExtendsTemplate = R"(
  //=======================
  //=== STRUCTS EXTENDS ===
  //=======================

${structExtends})";

  std::string           structExtends;
  std::set<std::string> listedStructs;
  for ( auto const & feature : m_features )
  {
    structExtends += generateStructExtendsStructs( feature.requireData, listedStructs, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    structExtends += generateStructExtendsStructs( extension.requireData, listedStructs, extension.name );
  }

  return replaceWithMap( structExtendsTemplate, { { "structExtends", structExtends } } );
}

std::string VulkanHppGenerator::generateStructExtendsStructs( std::vector<RequireData> const & requireData,
                                                              std::set<std::string> &          listedStructs,
                                                              std::string const &              title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto structIt = m_structs.find( type );
      if ( ( structIt != m_structs.end() ) && listedStructs.insert( type ).second )
      {
        auto [enter, leave] = generateProtection( getProtectFromTitle( title ) );

        // append all allowed structure chains
        for ( auto extendName : structIt->second.structExtends )
        {
          std::map<std::string, StructureData>::const_iterator itExtend = m_structs.find( extendName );
          if ( itExtend == m_structs.end() )
          {
            // look if the extendName acutally is an alias of some other structure
            auto aliasIt = m_structAliases.find( extendName );
            if ( aliasIt != m_structAliases.end() )
            {
              itExtend = m_structs.find( aliasIt->second.name );
              assert( itExtend != m_structs.end() );
            }
          }

          auto [subEnter, subLeave] = generateProtection( getProtectFromType( itExtend->first ) );

          if ( enter != subEnter )
          {
            str += subEnter;
          }

          str += "  template <> struct StructExtends<" + stripPrefix( structIt->first, "Vk" ) + ", " + stripPrefix( extendName, "Vk" ) +
                 ">{ enum { value = true }; };\n";

          if ( leave != subLeave )
          {
            str += subLeave;
          }
        }
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateStructForwardDeclarations() const
{
  const std::string fowardDeclarationsTemplate = R"(
  //===================================
  //=== STRUCT forward declarations ===
  //===================================

${forwardDeclarations}
)";

  std::string           forwardDeclarations;
  std::set<std::string> listedStructs;
  for ( auto const & feature : m_features )
  {
    forwardDeclarations += generateStructForwardDeclarations( feature.requireData, feature.name, listedStructs );
  }
  for ( auto const & extension : m_extensions )
  {
    forwardDeclarations += generateStructForwardDeclarations( extension.requireData, extension.name, listedStructs );
  }

  return replaceWithMap( fowardDeclarationsTemplate, { { "forwardDeclarations", forwardDeclarations } } );
}

std::string VulkanHppGenerator::generateStructForwardDeclarations( std::vector<RequireData> const & requireData,
                                                                   std::string const &              title,
                                                                   std::set<std::string> &          listedStructs ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto structIt = m_structs.find( type );
      if ( ( structIt != m_structs.end() ) && listedStructs.insert( type ).second )
      {
        std::string structureType = stripPrefix( structIt->first, "Vk" );
        str += ( structIt->second.isUnion ? "  union " : "  struct " ) + structureType + ";\n";

        for ( auto const & alias : m_structAliases )
        {
          if ( alias.second.name == type )
          {
            str += "  using " + stripPrefix( alias.first, "Vk" ) + " = " + structureType + ";\n";
          }
        }
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::tuple<std::string, std::string, std::string, std::string>
  VulkanHppGenerator::generateStructMembers( std::pair<std::string, StructureData> const & structData ) const
{
  std::string members, memberNames, memberTypes, sTypeValue;
  for ( auto const & member : structData.second.members )
  {
    members += "    ";
    std::string type;
    if ( !member.bitCount.empty() && member.type.type.starts_with( "Vk" ) )
    {
      assert( member.type.prefix.empty() && member.type.postfix.empty() );  // never encounterd a different case
      type = member.type.type;
    }
    else if ( member.arraySizes.empty() )
    {
      type = member.type.compose( "VULKAN_HPP_NAMESPACE" );
    }
    else
    {
      assert( member.type.prefix.empty() && member.type.postfix.empty() );
      type = generateStandardArrayWrapper( member.type.compose( "VULKAN_HPP_NAMESPACE" ), member.arraySizes );
    }
    members += type + " " + member.name;
    if ( !member.value.empty() )
    {
      // special handling for members with legal value: use it as the default
      members += " = ";
      if ( member.type.type == "uint32_t" )
      {
        members += member.value;
      }
      else
      {
        auto enumIt = m_enums.find( member.type.type );
        assert( enumIt != m_enums.end() );
        std::string enumValue = member.value;
        auto        valueIt   = std::find_if(
          enumIt->second.values.begin(), enumIt->second.values.end(), [&enumValue]( EnumValueData const & evd ) { return enumValue == evd.name; } );
        assert( valueIt != enumIt->second.values.end() );
        std::string valueName = generateEnumValueName( enumIt->first, valueIt->name, enumIt->second.isBitmask );
        members += stripPrefix( member.type.type, "Vk" ) + "::" + valueName;
        if ( member.name == "sType" )
        {
          sTypeValue = valueName;
        }
      }
    }
    else
    {
      // as we don't have any meaningful default initialization values, everything can be initialized by just '{}'
      // !
      assert( member.arraySizes.empty() || member.bitCount.empty() );
      if ( !member.bitCount.empty() )
      {
        members += " : " + member.bitCount;  // except for bitfield members, where no default member initializatin
                                             // is supported (up to C++20)
      }
      else
      {
        members += " = ";
        auto enumIt = m_enums.find( member.type.type );
        if ( member.arraySizes.empty() && ( enumIt != m_enums.end() ) && member.type.postfix.empty() )
        {
          members += generateEnumInitializer( member.type, member.arraySizes, enumIt->second.values, enumIt->second.isBitmask );
        }
        else
        {
          members += "{}";
        }
      }
    }
    members += ";\n";

    memberNames += member.name + ", ";
    memberTypes += type + " const &, ";
  }
  return std::make_tuple( members, stripPostfix( memberNames, ", " ), stripPostfix( memberTypes, ", " ), sTypeValue );
}

std::string VulkanHppGenerator::generateStructSetter( std::string const & structureName, std::vector<MemberData> const & memberData, size_t index ) const
{
  std::string        str;
  MemberData const & member = memberData[index];
  if ( member.type.type != "VkStructureType" )  // filter out StructureType, which is supposed to be immutable !
  {
    static const std::string templateString = R"(
    ${constexpr}${structureName} & set${MemberName}( ${memberType} ${reference}${memberName}_ ) VULKAN_HPP_NOEXCEPT
    {
      ${assignment};
      return *this;
    }
)";

    std::string memberType         = member.arraySizes.empty() ? member.type.compose( "VULKAN_HPP_NAMESPACE" )
                                                               : generateStandardArray( member.type.compose( "VULKAN_HPP_NAMESPACE" ), member.arraySizes );
    bool        isReinterpretation = !member.bitCount.empty() && member.type.type.starts_with( "Vk" );
    std::string assignment;
    if ( isReinterpretation )
    {
      assignment = member.name + " = " + "*reinterpret_cast<" + member.type.type + "*>(&" + member.name + "_)";
    }
    else
    {
      assignment = member.name + " = " + member.name + "_";
    }

    str += replaceWithMap( templateString,
                           { { "assignment", assignment },
                             { "constexpr", isReinterpretation ? "" : "VULKAN_HPP_CONSTEXPR_14 " },
                             { "memberName", member.name },
                             { "MemberName", startUpperCase( member.name ) },
                             { "memberType", memberType },
                             { "reference", ( member.type.postfix.empty() && ( m_structs.find( member.type.type ) != m_structs.end() ) ) ? "const & " : "" },
                             { "structureName", structureName } } );

    if ( hasLen( member ) )
    {
      assert( member.name.front() == 'p' );
      std::string arrayName = startLowerCase( stripPrefix( member.name, "p" ) );

      std::string lenName, lenValue;
      if ( member.lenExpressions[0] == "codeSize / 4" )
      {
        lenName  = "codeSize";
        lenValue = arrayName + "_.size() * 4";
      }
      else
      {
        lenName  = member.lenExpressions[0];
        lenValue = arrayName + "_.size()";
      }

      assert( memberType.back() == '*' );
      memberType = trimEnd( stripPostfix( memberType, "*" ) );

      std::string templateHeader;
      if ( ( member.type.type == "void" ) && ( memberType.find( '*' ) == std::string::npos ) )
      {
        assert( templateHeader.empty() );
        templateHeader = "template <typename T>\n    ";

        size_t pos = memberType.find( "void" );
        assert( pos != std::string::npos );
        memberType.replace( pos, strlen( "void" ), "T" );

        lenValue += " * sizeof(T)";
      }

      auto lenMember = findStructMemberIt( lenName, memberData );
      assert( lenMember != memberData.end() && lenMember->type.prefix.empty() && lenMember->type.postfix.empty() );
      if ( lenMember->type.type != "size_t" )
      {
        lenValue = "static_cast<" + lenMember->type.type + ">( " + lenValue + " )";
      }

      static const std::string setArrayTemplate = R"(
#if !defined( VULKAN_HPP_DISABLE_ENHANCED_MODE )
    ${templateHeader}${structureName} & set${ArrayName}( VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<${memberType}> const & ${arrayName}_ ) VULKAN_HPP_NOEXCEPT
    {
      ${lenName} = ${lenValue};
      ${memberName} = ${arrayName}_.data();
      return *this;
    }
#endif /*VULKAN_HPP_DISABLE_ENHANCED_MODE*/
)";

      str += replaceWithMap( setArrayTemplate,
                             { { "arrayName", arrayName },
                               { "ArrayName", startUpperCase( arrayName ) },
                               { "lenName", lenName },
                               { "lenValue", lenValue },
                               { "memberName", member.name },
                               { "memberType", memberType },
                               { "structureName", structureName },
                               { "templateHeader", templateHeader } } );
    }
  }
  return str;
}

std::string VulkanHppGenerator::generateStructSubConstructor( std::pair<std::string, StructureData> const & structData ) const
{
  if ( !structData.second.subStruct.empty() )
  {
    auto const & subStruct = m_structs.find( structData.second.subStruct );
    assert( subStruct != m_structs.end() );

    std::string subStructArgumentName = startLowerCase( stripPrefix( subStruct->first, "Vk" ) );

    std::string subCopies;
    bool        firstArgument = true;
    for ( size_t i = 0; i < subStruct->second.members.size(); i++ )
    {
      assert( structData.second.members[i].arraySizes.empty() );
      static const std::string subCopiesTemplate =
        R"(      ${separator} ${structMemberName}( ${subStructArgumentName}.${subStructMemberName} )
)";
      subCopies += replaceWithMap( subCopiesTemplate,
                                   { { "separator", firstArgument ? ":" : "," },
                                     { "structMemberName", structData.second.members[i].name },
                                     { "subStructMemberName", subStruct->second.members[i].name },
                                     { "subStructArgumentName", subStructArgumentName } } );
      firstArgument = false;
    }

    std::string subArguments;
    bool        listedArgument = true;
    for ( size_t i = subStruct->second.members.size(); i < structData.second.members.size(); i++ )
    {
      std::string argument = generateStructConstructorArgument( listedArgument, structData.second.members[i], true );
      if ( !argument.empty() )
      {
        listedArgument = true;
        subArguments += argument;
      }

      assert( structData.second.members[i].arraySizes.empty() );
      static const std::string subCopiesTemplate = R"(    , ${memberName}( ${memberName}_ )
)";
      subCopies += replaceWithMap( subCopiesTemplate, { { "memberName", structData.second.members[i].name } } );
    }

    static const std::string subStructConstructorTemplate = R"(
    explicit ${structName}( ${subStructName} const & ${subStructArgumentName}${subArguments} )
${subCopies}    {}
)";
    return replaceWithMap( subStructConstructorTemplate,
                           { { "structName", stripPrefix( structData.first, "Vk" ) },
                             { "subArguments", subArguments },
                             { "subCopies", subCopies },
                             { "subStructArgumentName", subStructArgumentName },
                             { "subStructName", stripPrefix( subStruct->first, "Vk" ) } } );
  }
  return "";
}

std::string VulkanHppGenerator::generateSuccessCheck( std::vector<std::string> const & successCodes ) const
{
  assert( !successCodes.empty() );
  std::string successCheck = "result == " + generateSuccessCode( successCodes[0] );
  if ( 1 < successCodes.size() )
  {
    successCheck = "( " + successCheck + " )";
    for ( size_t i = 1; i < successCodes.size(); ++i )
    {
      successCheck += "|| ( result == " + generateSuccessCode( successCodes[i] ) + " )";
    }
  }
  return successCheck;
}

std::string VulkanHppGenerator::generateSuccessCode( std::string const & code ) const
{
  std::string tag = findTag( code );
  // on each success code: prepend 'VULKAN_HPP_NAMESPACE::Result::e', strip "VK_" and a tag, convert it to camel
  // case, and add the tag again
  return "VULKAN_HPP_NAMESPACE::Result::e" + toCamelCase( stripPostfix( stripPrefix( code, "VK_" ), tag ) ) + tag;
}

std::string VulkanHppGenerator::generateSuccessCodeList( std::vector<std::string> const & successCodes, bool enumerating ) const
{
  std::string successCodeList;
  if ( ( 1 < successCodes.size() ) && !enumerating )
  {
    successCodeList = ", { " + generateSuccessCode( successCodes[0] );
    for ( size_t i = 1; i < successCodes.size(); ++i )
    {
      successCodeList += ", " + generateSuccessCode( successCodes[i] );
    }
    successCodeList += " }";
  }
  return successCodeList;
}

std::string VulkanHppGenerator::generateThrowResultException() const
{
  auto enumIt = m_enums.find( "VkResult" );

  std::string cases;
  for ( auto const & value : enumIt->second.values )
  {
    // only generate throw-functions for non-alias error codes
    if ( value.alias.empty() && value.name.starts_with( "VK_ERROR" ) )
    {
      auto [enter, leave]   = generateProtection( value.protect );
      std::string valueName = generateEnumValueName( enumIt->first, value.name, false );
      cases += enter + "      case Result::" + valueName + ": throw " + stripPrefix( valueName, "eError" ) + "Error( message );\n" + leave;
    }
  }
  cases.pop_back();  // remove last newline

  const std::string throwTemplate = R"(
  namespace
  {
    [[noreturn]] void throwResultException( Result result, char const * message )
    {
      switch ( result )
      {
${cases}
        default: throw SystemError( make_error_code( result ), message );
      }
    }
  })";
  return replaceWithMap( throwTemplate, { { "cases", cases } } );
}

std::string VulkanHppGenerator::generateTypenameCheck( std::vector<size_t> const &               returnParams,
                                                       std::map<size_t, VectorParamData> const & vectorParams,
                                                       bool                                      definition,
                                                       std::vector<std::string> const &          dataTypes,
                                                       CommandFlavourFlags                       flavourFlags ) const
{
  std::string typenameCheck, enableIf;
  if ( !( flavourFlags & CommandFlavourFlagBits::singular ) && ( flavourFlags & CommandFlavourFlagBits::withAllocator ) )
  {
    for ( size_t i = 0; i < returnParams.size(); ++i )
    {
      if ( vectorParams.find( returnParams[i] ) != vectorParams.end() )
      {
        std::string elementType = ( flavourFlags & CommandFlavourFlagBits::chained ) ? "StructureChain" : stripPrefix( dataTypes[i], "VULKAN_HPP_NAMESPACE::" );
        std::string extendedElementType = elementType;
        if ( flavourFlags & CommandFlavourFlagBits::unique )
        {
          extendedElementType = "UniqueHandle<" + elementType + ", Dispatch>";
        }
        std::string index = std::to_string( i );
        if ( definition )
        {
          typenameCheck += ", typename B" + index;
        }
        else
        {
          typenameCheck += ", typename B" + index + " = " + startUpperCase( elementType ) + "Allocator";
        }
        enableIf += enableIf.empty() ? ", typename std::enable_if<" : " && ";
        enableIf += "std::is_same<typename B" + index + "::value_type, " + extendedElementType + ">::value";
      }
    }
    assert( !typenameCheck.empty() );
    if ( !typenameCheck.empty() )
    {
      typenameCheck += enableIf + ", int>::type";
      if ( !definition )
      {
        typenameCheck += " = 0";
      }
    }
  }
  return typenameCheck;
}

std::string VulkanHppGenerator::generateUnion( std::pair<std::string, StructureData> const & structure ) const
{
  auto [enter, leave]   = generateProtection( getProtectFromType( structure.first ) );
  std::string unionName = stripPrefix( structure.first, "Vk" );

  bool               firstMember = true;
  std::set<TypeInfo> listedTypes;  // create just one constructor per different type !
  std::string        constructors;
  for ( auto memberIt = structure.second.members.begin(); memberIt != structure.second.members.end(); ++memberIt )
  {
    if ( listedTypes.insert( memberIt->type ).second )
    {
      // VkBool32 is aliased to uint32_t. Don't create a VkBool32 constructor if the union also contains a
      // uint32_t constructor.
      if ( memberIt->type.type == "VkBool32" )
      {
        if ( findStructMemberItByType( "uint32_t", structure.second.members ) != structure.second.members.end() )
        {
          continue;
        }
      }

      bool multipleType =
        ( std::find_if( std::next( memberIt ),
                        structure.second.members.end(),
                        [memberIt]( MemberData const & member ) { return member.type == memberIt->type; } ) != structure.second.members.end() );
      std::string memberType = ( memberIt->arraySizes.empty() )
                               ? memberIt->type.compose( "VULKAN_HPP_NAMESPACE" )
                               : ( "const " + generateStandardArray( memberIt->type.compose( "VULKAN_HPP_NAMESPACE" ), memberIt->arraySizes ) + "&" );

      // In a majority of cases this can be constexpr in C++11 as well, however, determining when exactly
      // that is the case is a lot more involved and probably not worth it.
      static const std::string constructorTemplate = R"(
    VULKAN_HPP_CONSTEXPR_14 ${unionName}( ${memberType} ${argumentName}_${defaultAssignment} )
      : ${memberName}( ${argumentName}_ )
    {})";

      constructors += ( firstMember ? "" : "\n" ) + replaceWithMap( constructorTemplate,
                                                                    { { "argumentName", multipleType ? generateName( memberIt->type ) : memberIt->name },
                                                                      { "defaultAssignment", firstMember ? " = {}" : "" },
                                                                      { "memberName", memberIt->name },
                                                                      { "memberType", memberType },
                                                                      { "unionName", stripPrefix( structure.first, "Vk" ) } } );
      firstMember = false;

      if ( !memberIt->arraySizes.empty() )
      {
        assert( !multipleType );
        assert( memberIt->arraySizes.size() == 1 );
        int size = std::stoi( memberIt->arraySizes[0] );
        assert( std::to_string( size ) == memberIt->arraySizes[0] );
        std::string arguments, callArguments;
        bool        firstArgument = true;
        for ( int i = 0; i < size; i++ )
        {
          if ( !firstArgument )
          {
            arguments += ", ";
            callArguments += ", ";
          }
          std::string argumentIndex = std::to_string( i );
          arguments += memberIt->type.type + " " + memberIt->name + "_" + argumentIndex;
          callArguments += memberIt->name + "_" + argumentIndex;
          firstArgument = false;
        }

        static const std::string constructorBySequenceTemplate = R"(
    VULKAN_HPP_CONSTEXPR ${unionName}( ${arguments} )
      : ${memberName}{ { { ${callArguments} } } }
    {})";

        constructors += "\n" + replaceWithMap( constructorBySequenceTemplate,
                                               { { "arguments", arguments },
                                                 { "callArguments", callArguments },
                                                 { "memberName", memberIt->name },
                                                 { "unionName", stripPrefix( structure.first, "Vk" ) } } );
      }
    }
  }

  // one setter per union element
  std::string setters;
  for ( size_t i = 0; i < structure.second.members.size(); i++ )
  {
    setters += generateStructSetter( stripPrefix( structure.first, "Vk" ), structure.second.members, i );
  }
  // filter out leading and trailing newline
  setters = setters.substr( 1, setters.length() - 2 );

  // the union member variables
  std::string members;
  // if there's at least one Vk... type in this union, check for unrestricted unions support
  bool needsUnrestrictedUnions =
    ( std::find_if( structure.second.members.begin(),
                    structure.second.members.end(),
                    []( MemberData const & member ) { return member.type.type.starts_with( "Vk" ); } ) != structure.second.members.end() );
  if ( needsUnrestrictedUnions )
  {
    members += "#ifdef VULKAN_HPP_HAS_UNRESTRICTED_UNIONS\n";
  }
  for ( auto const & member : structure.second.members )
  {
    static const std::string memberTemplate = R"(    ${memberType} ${memberName};
)";
    members +=
      replaceWithMap( memberTemplate,
                      { { "memberName", member.name },
                        { "memberType",
                          member.arraySizes.empty() ? member.type.compose( "VULKAN_HPP_NAMESPACE" )
                                                    : generateStandardArrayWrapper( member.type.compose( "VULKAN_HPP_NAMESPACE" ), member.arraySizes ) } } );
  }
  if ( needsUnrestrictedUnions )
  {
    members += "#else\n";
    for ( auto const & member : structure.second.members )
    {
      static const std::string memberTemplate = R"(    ${memberType} ${memberName}${arraySizes};
)";
      members += replaceWithMap(
        memberTemplate,
        { { "arraySizes", generateCArraySizes( member.arraySizes ) }, { "memberName", member.name }, { "memberType", member.type.compose( "" ) } } );
    }
    members += "#endif  /*VULKAN_HPP_HAS_UNRESTRICTED_UNIONS*/\n";
  }

  static const std::string unionTemplate = R"(
${enter}  union ${unionName}
  {
    using NativeType = Vk${unionName};
#if !defined( VULKAN_HPP_NO_UNION_CONSTRUCTORS )
${constructors}
#endif /*VULKAN_HPP_NO_UNION_CONSTRUCTORS*/

#if !defined( VULKAN_HPP_NO_UNION_SETTERS )
${setters}
#endif /*VULKAN_HPP_NO_UNION_SETTERS*/

    operator Vk${unionName} const &() const
    {
      return *reinterpret_cast<const Vk${unionName}*>( this );
    }

    operator Vk${unionName} &()
    {
      return *reinterpret_cast<Vk${unionName}*>( this );
    }

${members}
  };
${leave})";

  return replaceWithMap(
    unionTemplate,
    { { "constructors", constructors }, { "enter", enter }, { "leave", leave }, { "members", members }, { "setters", setters }, { "unionName", unionName } } );
}

std::string VulkanHppGenerator::generateUniqueHandle( std::pair<std::string, HandleData> const & handleData ) const
{
  if ( !handleData.second.deleteCommand.empty() )
  {
    std::string type = stripPrefix( handleData.first, "Vk" );
    std::string aliasHandle;
    auto        aliasIt = findAlias( handleData.first, m_handleAliases );
    if ( aliasIt != m_handleAliases.end() )
    {
      static const std::string aliasHandleTemplate = R"(  using Unique${aliasType} = UniqueHandle<${type}, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>;)";

      aliasHandle += replaceWithMap( aliasHandleTemplate, { { "aliasType", stripPrefix( aliasIt->first, "Vk" ) }, { "type", type } } );
    }

    static const std::string uniqueHandleTemplate = R"(  template <typename Dispatch>
  class UniqueHandleTraits<${type}, Dispatch>
  {
  public:
    using deleter = ${deleterType}${deleterAction}<${deleterParent}${deleterPool}, Dispatch>;
  };
  using Unique${type} = UniqueHandle<${type}, VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>;
${aliasHandle})";

    return replaceWithMap( uniqueHandleTemplate,
                           { { "aliasHandle", aliasHandle },
                             { "deleterAction", ( handleData.second.deleteCommand.substr( 2, 4 ) == "Free" ) ? "Free" : "Destroy" },
                             { "deleterParent", handleData.second.deleteParent.empty() ? "NoParent" : stripPrefix( handleData.second.deleteParent, "Vk" ) },
                             { "deleterPool", handleData.second.deletePool.empty() ? "" : ", " + stripPrefix( handleData.second.deletePool, "Vk" ) },
                             { "deleterType", handleData.second.deletePool.empty() ? "Object" : "Pool" },
                             { "type", type } } );
  }
  return "";
}

std::string VulkanHppGenerator::generateUniqueHandle( std::vector<RequireData> const & requireData, std::string const & title ) const
{
  std::string str;
  for ( auto const & require : requireData )
  {
    for ( auto const & type : require.types )
    {
      auto handleIt = m_handles.find( type );
      if ( handleIt != m_handles.end() )
      {
        str += generateUniqueHandle( *handleIt );
      }
    }
  }
  return addTitleAndProtection( title, str );
}

std::string VulkanHppGenerator::generateUniqueHandles() const
{
  std::string uniqueHandlesTemplate = R"(
#ifndef VULKAN_HPP_NO_SMART_HANDLE
  //======================
  //=== UNIQUE HANDLEs ===
  //======================

${uniqueHandles}
#endif  /*VULKAN_HPP_NO_SMART_HANDLE*/
)";

  std::string uniqueHandles;
  for ( auto const & feature : m_features )
  {
    uniqueHandles += generateUniqueHandle( feature.requireData, feature.name );
  }
  for ( auto const & extension : m_extensions )
  {
    uniqueHandles += generateUniqueHandle( extension.requireData, extension.name );
  }
  assert( uniqueHandles.back() == '\n' );
  uniqueHandles.pop_back();
  return replaceWithMap( uniqueHandlesTemplate, { { "uniqueHandles", uniqueHandles } } );
}

std::string VulkanHppGenerator::generateVectorSizeCheck( std::string const &                           name,
                                                         CommandData const &                           commandData,
                                                         size_t                                        initialSkipCount,
                                                         std::map<size_t, std::vector<size_t>> const & countToVectorMap,
                                                         std::set<size_t> const &                      skippedParams,
                                                         bool                                          onlyThrows ) const
{
  std::string const assertTemplate = "    VULKAN_HPP_ASSERT( ${zeroSizeCheck}${firstVectorName}.size() == ${secondVectorName}.size() );";
  std::string const assertTemplateVoid =
    "    VULKAN_HPP_ASSERT( ${zeroSizeCheck}${firstVectorName}.size() * sizeof( ${firstDataType} ) == ${secondVectorName}.size() * sizeof( ${secondDataType} ) );";
  std::string const assertTemplateSingle = "    VULKAN_HPP_ASSERT( ${vectorName}.size() == ${sizeValue} );";
  std::string const throwTemplate =
    R"#(    if ( ${zeroSizeCheck}${firstVectorName}.size() != ${secondVectorName}.size() )
  {
    throw LogicError( VULKAN_HPP_NAMESPACE_STRING "::${className}::${commandName}: ${firstVectorName}.size() != ${secondVectorName}.size()" );
  })#";
  std::string const throwTemplateVoid =
    R"#(    if ( ${zeroSizeCheck}${firstVectorName}.size() * sizeof( ${firstDataType} ) != ${secondVectorName}.size() * sizeof( ${secondDataType} ) )
  {
    throw LogicError( VULKAN_HPP_NAMESPACE_STRING "::${className}::${commandName}: ${firstVectorName}.size() * sizeof( ${firstDataType} ) != ${secondVectorName}.size() * sizeof( ${secondDataType} )" );
  })#";
  std::string const throwTemplateSingle = R"#(    if ( ${vectorName}.size() != ${sizeValue} )
    {
      throw LogicError( VULKAN_HPP_NAMESPACE_STRING "::${className}::${commandName}: ${vectorName}.size() != ${sizeValue}" );
    })#";

  std::string className   = stripPrefix( commandData.params[initialSkipCount - 1].type.type, "Vk" );
  std::string commandName = generateCommandName( name, commandData.params, initialSkipCount );

  std::string assertions, throws;
  for ( auto const & cvm : countToVectorMap )
  {
    size_t      defaultStartIndex = determineDefaultStartIndex( commandData.params, skippedParams );
    std::string firstVectorName   = startLowerCase( stripPrefix( commandData.params[cvm.second[0]].name, "p" ) );

    if ( cvm.second.size() == 1 )
    {
      std::string sizeValue;
      if ( isLenByStructMember( commandData.params[cvm.second[0]].lenExpression, commandData.params[cvm.first] ) )
      {
        std::vector<std::string> lenParts = tokenize( commandData.params[cvm.second[0]].lenExpression, "->" );
        assert( lenParts.size() == 2 );
        sizeValue = startLowerCase( stripPrefix( lenParts[0], "p" ) ) + "." + lenParts[1];
      }
      else
      {
        assert( !commandData.params[cvm.second[0]].lenParams.empty() );
        if ( commandData.params[cvm.first].type.type == "VkSampleCountFlagBits" )
        {
          assert( commandData.params[cvm.second[0]].lenExpression == "(" + commandData.params[cvm.second[0]].lenParams[0].first + " + 31) / 32" );
          sizeValue = "( static_cast<uint32_t>( " + commandData.params[cvm.second[0]].lenParams[0].first + " ) + 31 ) / 32";
        }
        else
        {
          sizeValue = commandData.params[cvm.second[0]].lenExpression;
        }
      }
      assertions += replaceWithMap( assertTemplateSingle, { { "sizeValue", sizeValue }, { "vectorName", firstVectorName } } );
      throws += replaceWithMap( throwTemplateSingle,
                                { { "className", className }, { "commandName", commandName }, { "sizeValue", sizeValue }, { "vectorName", firstVectorName } } );
    }
    else
    {
      for ( size_t i = 1; i < cvm.second.size(); i++ )
      {
        std::string secondVectorName  = startLowerCase( stripPrefix( commandData.params[cvm.second[i]].name, "p" ) );
        bool        withZeroSizeCheck = commandData.params[cvm.second[i]].optional && ( defaultStartIndex <= cvm.second[i] );
        if ( commandData.params[cvm.second[0]].type.type == "void" )
        {
          assert( commandData.params[cvm.second[i]].type.type == "void" );
          std::string firstDataType  = stripPrefix( commandData.params[cvm.second[0]].name, "p" ) + "Type";
          std::string secondDataType = stripPrefix( commandData.params[cvm.second[i]].name, "p" ) + "Type";
          assertions += replaceWithMap( assertTemplateVoid,
                                        { { "firstDataType", firstDataType },
                                          { "firstVectorName", firstVectorName },
                                          { "secondDataType", secondDataType },
                                          { "secondVectorName", secondVectorName },
                                          { "zeroSizeCheck", withZeroSizeCheck ? ( secondVectorName + ".empty() || " ) : "" } } );
          throws += replaceWithMap( throwTemplateVoid,
                                    { { "firstDataType", firstDataType },
                                      { "firstVectorName", firstVectorName },
                                      { "className", className },
                                      { "commandName", commandName },
                                      { "secondDataType", secondDataType },
                                      { "secondVectorName", secondVectorName },
                                      { "zeroSizeCheck", withZeroSizeCheck ? ( "!" + secondVectorName + ".empty() && " ) : "" } } );
        }
        else
        {
          assertions += replaceWithMap( assertTemplate,
                                        { { "firstVectorName", firstVectorName },
                                          { "secondVectorName", secondVectorName },
                                          { "zeroSizeCheck", withZeroSizeCheck ? ( secondVectorName + ".empty() || " ) : "" } } );
          throws += replaceWithMap( throwTemplate,
                                    { { "firstVectorName", firstVectorName },
                                      { "className", className },
                                      { "commandName", commandName },
                                      { "secondVectorName", secondVectorName },
                                      { "zeroSizeCheck", withZeroSizeCheck ? ( "!" + secondVectorName + ".empty() && " ) : "" } } );
        }
        if ( i + 1 < cvm.second.size() )
        {
          assertions += "\n";
          throws += "\n";
        }
      }
    }
  }

  std::string const sizeCheckTemplate =
    R"(#ifdef VULKAN_HPP_NO_EXCEPTIONS
${assertions}
#else
${throws}
#endif  /*VULKAN_HPP_NO_EXCEPTIONS*/)";

  return onlyThrows ? throws : replaceWithMap( sizeCheckTemplate, { { "assertions", assertions }, { "throws", throws } } );
}

VulkanHppGenerator::CommandData const & VulkanHppGenerator::getCommandData( std::string const & command ) const
{
  auto commandIt = m_commands.find( command );
  if ( commandIt == m_commands.end() )
  {
    auto aliasIt = m_commandAliases.find( command );
    assert( aliasIt != m_commandAliases.end() );
    commandIt = m_commands.find( aliasIt->second.name );
    assert( commandIt != m_commands.end() );
  }
  return commandIt->second;
}

std::pair<std::string, std::string> VulkanHppGenerator::getParentTypeAndName( std::pair<std::string, HandleData> const & handle ) const
{
  std::string parentType, parentName;
  if ( handle.first == "VkInstance" )
  {
    parentType = "Context";
    parentName = "context";
  }
  else
  {
    bool skip = skipLeadingGrandParent( handle );
    assert( !handle.second.constructorIts.empty() && ( ( skip ? 1u : 0u ) < handle.second.constructorIts.front()->second.params.size() ) );
    auto const & param = handle.second.constructorIts.front()->second.params[skip ? 1 : 0];
    assert( isHandleType( param.type.type ) && param.type.isValue() );
    parentType = stripPrefix( param.type.type, "Vk" );
    parentName = param.name;
  }
  return std::make_pair( parentType, parentName );
}

std::string VulkanHppGenerator::getPlatform( std::string const & title ) const
{
  if ( !isSupportedFeature( title ) )
  {
    auto extensionIt = findSupportedExtension( title );
    assert( extensionIt != m_extensions.end() );
    return extensionIt->platform;
  }
  return "";
}

std::pair<std::string, std::string> VulkanHppGenerator::getPoolTypeAndName( std::string const & type ) const
{
  auto structIt = m_structs.find( type );
  assert( structIt != m_structs.end() );
  auto memberIt = std::find_if(
    structIt->second.members.begin(), structIt->second.members.end(), []( MemberData const & md ) { return md.name.find( "Pool" ) != std::string::npos; } );
  assert( memberIt != structIt->second.members.end() );
  assert( std::find_if( std::next( memberIt ),
                        structIt->second.members.end(),
                        []( MemberData const & md ) { return md.name.find( "Pool" ) != std::string::npos; } ) == structIt->second.members.end() );
  return std::make_pair( memberIt->type.type, memberIt->name );
}

std::string VulkanHppGenerator::getProtectFromPlatform( std::string const & platform ) const
{
  auto platformIt = m_platforms.find( platform );
  return ( platformIt != m_platforms.end() ) ? platformIt->second.protect : "";
}

std::string VulkanHppGenerator::getProtectFromTitle( std::string const & title ) const
{
  if ( !isSupportedFeature( title ) )
  {
    auto extensionIt = findSupportedExtension( title );
    return ( extensionIt != m_extensions.end() ) ? getProtectFromPlatform( extensionIt->platform ) : "";
  }
  return "";
}

std::string VulkanHppGenerator::getProtectFromTitles( std::set<std::string> const & titles ) const
{
  for ( auto titleIt = titles.begin(); titleIt != titles.end(); ++titleIt )
  {
    std::string protect = getProtectFromTitle( *titleIt );
    if ( !protect.empty() )
    {
#if !defined( NDEBUG )
      for ( titleIt = std::next( titleIt ); titleIt != titles.end(); ++titleIt )
      {
        std::string p = getProtectFromTitle( *titleIt );
        assert( p.empty() || ( p == protect ) );
      }
#endif
      return protect;
    }
  }
  return "";
}

std::string VulkanHppGenerator::getProtectFromType( std::string const & type ) const
{
  auto typeIt = m_types.find( type );
  assert( typeIt != m_types.end() );
  return getProtectFromTitles( typeIt->second.requiredBy );
}

std::string VulkanHppGenerator::getVectorSize( std::vector<ParamData> const &            params,
                                               std::map<size_t, VectorParamData> const & vectorParams,
                                               size_t                                    returnParam,
                                               std::string const &                       returnParamType,
                                               std::set<size_t> const &                  templatedParams ) const
{
  std::string              vectorSize;
  std::vector<std::string> lenParts = tokenize( params[returnParam].lenExpression, "->" );
  switch ( lenParts.size() )
  {
    case 1:
      {
        std::string const & len = lenParts[0];
        size_t              lenIdx =
          std::distance( params.begin(), std::find_if( params.begin(), params.end(), [&len]( ParamData const & pd ) { return pd.name == len; } ) );
        assert( lenIdx < params.size() );
        // look for the len, not being the len of the return param, but of an other vector param
        auto lenVectorParamIt =
          std::find_if( vectorParams.begin(),
                        vectorParams.end(),
                        [&lenIdx, &returnParam]( auto const & vpi ) { return ( vpi.first != returnParam ) && ( vpi.second.lenParam == lenIdx ); } );
        if ( lenVectorParamIt == vectorParams.end() )
        {
          vectorSize = lenParts[0];
          if ( templatedParams.find( returnParam ) != templatedParams.end() )
          {
            vectorSize += " / sizeof( " + returnParamType + " )";
          }
        }
        else
        {
          assert( templatedParams.find( returnParam ) == templatedParams.end() );
          vectorSize = startLowerCase( stripPrefix( params[lenVectorParamIt->first].name, "p" ) ) + ".size()";
        }
      }
      break;
    case 2:
      assert( vectorParams.find( returnParam ) != vectorParams.end() );
      vectorSize = startLowerCase( stripPrefix( lenParts[0], "p" ) ) + "." + lenParts[1];
      break;
    default: assert( false ); break;
  }
  assert( !vectorSize.empty() );
  return vectorSize;
}

void VulkanHppGenerator::handleRemoval( RemoveData const & removeData )
{
  for ( auto const & command : removeData.commands )
  {
    bool removed   = false;
    auto commandIt = m_commands.find( command );
    if ( commandIt == m_commands.end() )
    {
      auto aliasIt = m_commandAliases.find( command );
      assert( aliasIt != m_commandAliases.end() );
      commandIt = m_commands.find( aliasIt->second.name );
      assert( commandIt != m_commands.end() );
    }
    for ( auto const & requiredBy : commandIt->second.requiredBy )
    {
      auto featureIt = std::find_if( m_features.begin(), m_features.end(), [&requiredBy]( FeatureData const & fd ) { return fd.name == requiredBy; } );
      if ( featureIt != m_features.end() )
      {
        removed |= handleRemovalCommand( command, featureIt->requireData );
      }
      else
      {
        auto extensionIt =
          std::find_if( m_extensions.begin(), m_extensions.end(), [&requiredBy]( ExtensionData const & ed ) { return ed.name == requiredBy; } );
        if ( extensionIt != m_extensions.end() )
        {
          removed |= handleRemovalCommand( command, extensionIt->requireData );
        }
        else
        {
          assert( m_unsupportedExtensions.find( requiredBy ) != m_unsupportedExtensions.end() );
          removed = true;
        }
      }
    }
    checkForError( removed, commandIt->second.xmlLine, "to be removed command <" + commandIt->first + "> could not be removed" );
  }

  for ( auto const & e : removeData.enums )
  {
    bool removed = false;
    for ( auto enumIt = m_enums.begin(); !removed && enumIt != m_enums.end(); ++enumIt )
    {
      auto valueIt = std::find_if( enumIt->second.values.begin(), enumIt->second.values.end(), [&e]( EnumValueData const evd ) { return evd.name == e; } );
      if ( valueIt != enumIt->second.values.end() )
      {
        enumIt->second.values.erase( valueIt );
        removed = true;
      }
    }
    checkForError( removed, 0, "to be removed enum value <" + e + "> could not be removed" );
  }

  for ( auto const & type : removeData.types )
  {
    auto typeIt = m_types.find( type );
    assert( typeIt != m_types.end() );
    bool removed = typeIt->second.requiredBy.empty();
    for ( auto const & requiredBy : typeIt->second.requiredBy )
    {
      auto featureIt = std::find_if( m_features.begin(), m_features.end(), [&requiredBy]( FeatureData const & fd ) { return fd.name == requiredBy; } );
      if ( featureIt != m_features.end() )
      {
        removed |= handleRemovalType( type, featureIt->requireData );
      }
      else
      {
        auto extensionIt =
          std::find_if( m_extensions.begin(), m_extensions.end(), [&requiredBy]( ExtensionData const & ed ) { return ed.name == requiredBy; } );
        if ( extensionIt != m_extensions.end() )
        {
          removed |= handleRemovalType( type, extensionIt->requireData );
        }
        else
        {
          assert( m_unsupportedExtensions.find( requiredBy ) != m_unsupportedExtensions.end() );
          removed = true;
        }
      }
    }
    checkForError( removed, typeIt->second.xmlLine, "to be removed type <" + typeIt->first + "> could not be removed" );
  }
}

bool VulkanHppGenerator::handleRemovalCommand( std::string const & command, std::vector<RequireData> & requireData )
{
  bool removed = false;
  for ( auto requireDataIt = requireData.begin(); !removed && ( requireDataIt != requireData.end() ); ++requireDataIt )
  {
    auto requireCommandIt = std::find( requireDataIt->commands.begin(), requireDataIt->commands.end(), command );
    if ( requireCommandIt != requireDataIt->commands.end() )
    {
      assert( std::find( std::next( requireCommandIt ), requireDataIt->commands.end(), command ) == requireDataIt->commands.end() );
      requireDataIt->commands.erase( requireCommandIt );
      assert( !requireDataIt->commands.empty() || !requireDataIt->types.empty() );
      removed = true;

#if !defined( NDEBUG )
      for ( auto it = std::next( requireDataIt ); it != requireData.end(); ++it )
      {
        assert( std::find( it->commands.begin(), it->commands.end(), command ) == it->commands.end() );
      }
#endif
    }
  }
  return removed;
}

void VulkanHppGenerator::handleRemovals()
{
  for ( auto const & feature : m_features )
  {
    for ( auto const & removeData : feature.removeData )
    {
      handleRemoval( removeData );
    }
  }
}

bool VulkanHppGenerator::handleRemovalType( std::string const & type, std::vector<RequireData> & requireData )
{
  bool removed = false;
  for ( auto requireDataIt = requireData.begin(); !removed && ( requireDataIt != requireData.end() ); )
  {
    auto requireTypeIt = std::find( requireDataIt->types.begin(), requireDataIt->types.end(), type );
    if ( requireTypeIt != requireDataIt->types.end() )
    {
      assert( std::find( std::next( requireTypeIt ), requireDataIt->types.end(), type ) == requireDataIt->types.end() );
      requireDataIt->types.erase( requireTypeIt );
      requireDataIt = ( requireDataIt->commands.empty() && requireDataIt->types.empty() ) ? requireData.erase( requireDataIt ) : std::next( requireDataIt );
      removed       = true;

#if !defined( NDEBUG )
      for ( auto it = std::next( requireDataIt ); it != requireData.end(); ++it )
      {
        assert( std::find( it->types.begin(), it->types.end(), type ) == it->types.end() );
      }
#endif
    }
    else
    {
      requireDataIt = std::next( requireDataIt );
    }
  }
  return removed;
}

bool VulkanHppGenerator::hasLen( MemberData const & memberData ) const
{
  assert( memberData.lenMembers.size() <= memberData.lenExpressions.size() );
  return !memberData.lenMembers.empty() && ( ( memberData.lenExpressions[0] == memberData.lenMembers[0].first ) ||
                                             ( memberData.lenExpressions[0] == ( memberData.lenMembers[0].first + " / 4" ) ) );
}

bool VulkanHppGenerator::hasParentHandle( std::string const & handle, std::string const & parent ) const
{
  std::string candidate = handle;
  while ( !candidate.empty() )
  {
    auto const & handleIt = m_handles.find( candidate );
    assert( handleIt != m_handles.end() );
    if ( handleIt->second.parent == parent )
    {
      return true;
    }
    else
    {
      candidate = handleIt->second.parent;
    }
  }
  return false;
}

bool VulkanHppGenerator::isDeviceCommand( CommandData const & commandData ) const
{
  return !commandData.handle.empty() && !commandData.params.empty() && ( m_handles.find( commandData.params[0].type.type ) != m_handles.end() ) &&
         ( commandData.params[0].type.type != "VkInstance" ) && ( commandData.params[0].type.type != "VkPhysicalDevice" );
}

bool VulkanHppGenerator::isExtension( std::string const & name ) const
{
  return isSupportedExtension( name ) || ( m_unsupportedExtensions.find( name ) != m_unsupportedExtensions.end() );
}

bool VulkanHppGenerator::isFeature( std::string const & name ) const
{
  return isSupportedFeature( name ) || ( m_unsupportedFeatures.find( name ) != m_unsupportedFeatures.end() );
}

bool VulkanHppGenerator::isHandleType( std::string const & type ) const
{
  if ( type.starts_with( "Vk" ) )
  {
    auto it = m_handles.find( type );
    if ( it == m_handles.end() )
    {
      auto aliasIt = m_handleAliases.find( type );
      if ( aliasIt != m_handleAliases.end() )
      {
        it = m_handles.find( aliasIt->second.name );
        assert( it != m_handles.end() );
      }
    }
    return ( it != m_handles.end() );
  }
  return false;
}

bool VulkanHppGenerator::isLenByStructMember( std::string const & name, std::vector<ParamData> const & params ) const
{
  // check if name specifies a member of a struct
  std::vector<std::string> nameParts = tokenize( name, "->" );
  if ( nameParts.size() == 1 )
  {
    // older versions of vk.xml used the notation parameter::member
    nameParts = tokenize( name, "::" );
  }
  if ( nameParts.size() == 2 )
  {
    auto paramIt = std::find_if( params.begin(), params.end(), [&n = nameParts[0]]( ParamData const & pd ) { return pd.name == n; } );
    if ( paramIt != params.end() )
    {
#if !defined( NDEBUG )
      auto structureIt = m_structs.find( paramIt->type.type );
      assert( structureIt != m_structs.end() );
      assert( isStructMember( nameParts[1], structureIt->second.members ) );
#endif
      return true;
    }
  }
  return false;
}

bool VulkanHppGenerator::isLenByStructMember( std::string const & name, ParamData const & param ) const
{
  // check if name specifies a member of a struct
  std::vector<std::string> nameParts = tokenize( name, "->" );
  if ( nameParts.size() == 1 )
  {
    // older versions of vk.xml used the notation parameter::member
    nameParts = tokenize( name, "::" );
  }
  if ( ( nameParts.size() == 2 ) && ( nameParts[0] == param.name ) )
  {
#if !defined( NDEBUG )
    auto structureIt = m_structs.find( param.type.type );
    assert( structureIt != m_structs.end() );
    assert( isStructMember( nameParts[1], structureIt->second.members ) );
#endif
    return true;
  }
  return false;
}

bool VulkanHppGenerator::isMultiSuccessCodeConstructor( std::vector<std::map<std::string, CommandData>::const_iterator> const & constructorIts ) const
{
  bool ok = !constructorIts.empty();
  if ( ok )
  {
    auto constructorIt = constructorIts.begin();
    ok                 = ( 2 < ( *constructorIt )->second.successCodes.size() ) ||
         ( ( ( *constructorIt )->second.successCodes.size() == 2 ) && ( ( *constructorIt )->second.successCodes[1] != "VK_INCOMPLETE" ) );
#if !defined( NDEBUG )
    for ( constructorIt = std::next( constructorIt ); constructorIt != constructorIts.end(); ++constructorIt )
    {
      assert( ok == ( 2 < ( *constructorIt )->second.successCodes.size() ) ||
              ( ( ( *constructorIt )->second.successCodes.size() == 2 ) && ( ( *constructorIt )->second.successCodes[1] != "VK_INCOMPLETE" ) ) );
    }
#endif
  }
  return ok;
}

bool VulkanHppGenerator::isParam( std::string const & name, std::vector<ParamData> const & params ) const
{
  return findParamIt( name, params ) != params.end();
}

bool VulkanHppGenerator::isStructMember( std::string const & name, std::vector<MemberData> const & memberData ) const
{
  return findStructMemberIt( name, memberData ) != memberData.end();
}

bool VulkanHppGenerator::isStructureChainAnchor( std::string const & type ) const
{
  if ( type.starts_with( "Vk" ) )
  {
    auto it = m_structs.find( type );
    if ( it == m_structs.end() )
    {
      auto aliasIt = m_structAliases.find( type );
      if ( aliasIt != m_structAliases.end() )
      {
        it = m_structs.find( aliasIt->second.name );
      }
    }
    if ( it != m_structs.end() )
    {
      return m_extendedStructs.find( it->first ) != m_extendedStructs.end();
    }
  }
  return false;
}

bool VulkanHppGenerator::isSupported( std::set<std::string> const & requiredBy ) const
{
  for ( auto const & r : requiredBy )
  {
    if ( isSupportedFeature( r ) || isSupportedExtension( r ) )
    {
      return true;
    }
    else
    {
      assert( ( m_unsupportedFeatures.find( r ) != m_unsupportedFeatures.end() ) || ( m_unsupportedExtensions.find( r ) != m_unsupportedExtensions.end() ) );
    }
  }
  return false;
}

bool VulkanHppGenerator::isSupportedExtension( std::string const & name ) const
{
  return std::find_if( m_extensions.begin(), m_extensions.end(), [&name]( ExtensionData const & ed ) { return ed.name == name; } ) != m_extensions.end();
}

bool VulkanHppGenerator::isSupportedFeature( std::string const & name ) const
{
  return std::find_if( m_features.begin(), m_features.end(), [&name]( FeatureData const & fd ) { return fd.name == name; } ) != m_features.end();
}

bool VulkanHppGenerator::isTypeRequired( std::string const & type ) const
{
  auto typeIt = m_types.find( type );
  assert( typeIt != m_types.end() );
  return !typeIt->second.requiredBy.empty();
}

bool VulkanHppGenerator::isTypeUsed( std::string const & type ) const
{
  auto typeIt = m_types.find( type );
  assert( typeIt != m_types.end() );
  for ( auto const & require : typeIt->second.requiredBy )
  {
    auto featureIt = findFeature( require );
    if ( featureIt != m_features.end() )
    {
      for ( auto const & r : featureIt->requireData )
      {
        if ( std::find( r.types.begin(), r.types.end(), type ) != r.types.end() )
        {
          return true;
        }
      }
    }
    else
    {
      auto extensionIt = findSupportedExtension( require );
      if ( extensionIt != m_extensions.end() )
      {
        for ( auto const & r : extensionIt->requireData )
        {
          if ( std::find( r.types.begin(), r.types.end(), type ) != r.types.end() )
          {
            return true;
          }
        }
      }
    }
  }
  return false;
}

std::pair<bool, std::map<size_t, std::vector<size_t>>> VulkanHppGenerator::needsVectorSizeCheck( std::vector<ParamData> const &            params,
                                                                                                 std::map<size_t, VectorParamData> const & vectorParams,
                                                                                                 std::vector<size_t> const &               returnParams,
                                                                                                 std::set<size_t> const &                  singularParams,
                                                                                                 std::set<size_t> const &                  skippedParams ) const
{
  std::map<size_t, std::vector<size_t>> countToVectorMap;
  for ( auto const & vpi : vectorParams )
  {
    if ( ( vpi.second.lenParam != INVALID_INDEX ) && ( std::find( returnParams.begin(), returnParams.end(), vpi.first ) == returnParams.end() ) &&
         ( ( singularParams.find( vpi.second.lenParam ) == singularParams.end() ) ||
           isLenByStructMember( params[vpi.first].lenExpression, params[vpi.second.lenParam] ) ) )
    {
      countToVectorMap[vpi.second.lenParam].push_back( vpi.first );
    }
  }
  return std::make_pair( std::find_if( countToVectorMap.begin(),
                                       countToVectorMap.end(),
                                       [this, &params, &skippedParams]( auto const & cvm )
                                       {
                                         return ( 1 < cvm.second.size() ) || isLenByStructMember( params[cvm.second[0]].lenExpression, params[cvm.first] ) ||
                                                ( std::find_if( params[cvm.second[0]].lenParams.begin(),
                                                                params[cvm.second[0]].lenParams.end(),
                                                                [&skippedParams]( std::pair<std::string, size_t> const & lenParam ) {
                                                                  return skippedParams.find( lenParam.second ) != skippedParams.end();
                                                                } ) == params[cvm.second[0]].lenParams.end() );
                                       } ) != countToVectorMap.end(),
                         countToVectorMap );
}

void VulkanHppGenerator::readCommand( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  auto                               aliasIt    = attributes.find( "alias" );
  if ( aliasIt != attributes.end() )
  {
    checkAttributes( line, attributes, { { "alias", {} }, { "name", {} } }, {} );

    std::string alias = aliasIt->second;
    std::string name  = attributes.find( "name" )->second;

    checkForError( name.starts_with( "vk" ), line, "name <" + name + "> should begin with \"vk\"" );
    checkForError( m_commands.find( alias ) != m_commands.end(), line, "command <" + name + "> is aliased to unknown command <" + alias + ">" );
    checkForError( m_commandAliases.insert( { name, { alias, line } } ).second, line, "command <" + name + "> already specified as alias" );
  }
  else
  {
    checkAttributes( line,
                     attributes,
                     {},
                     { { "api", { "vulkan", "vulkansc" } },
                       { "cmdbufferlevel", { "primary", "secondary" } },
                       { "comment", {} },
                       { "errorcodes", {} },
                       { "queues", { "compute", "decode", "encode", "graphics", "opticalflow", "sparse_binding", "transfer" } },
                       { "renderpass", { "both", "inside", "outside" } },
                       { "successcodes", {} },
                       { "tasks", { "action", "indirection", "state", "synchronization" } },
                       { "videocoding", { "both", "inside", "outside" } } } );

    std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
    checkElements( line, children, { { "param", false }, { "proto", true } }, { "implicitexternsyncparams" } );

    CommandData commandData;
    commandData.xmlLine = line;
    std::string api;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "api" )
      {
        api = attribute.second;
      }
      else if ( attribute.first == "errorcodes" )
      {
        commandData.errorCodes = tokenize( attribute.second, "," );
        // errorCodes are checked in checkCorrectness after complete reading
      }
      else if ( attribute.first == "successcodes" )
      {
        commandData.successCodes = tokenize( attribute.second, "," );
        // successCodes are checked in checkCorrectness after complete reading
      }
    }

    std::string name;
    for ( auto child : children )
    {
      std::string value = child->Value();
      if ( value == "param" )
      {
        std::pair<bool, ParamData> result = readCommandParam( child, commandData.params );
        if ( result.first )
        {
          commandData.params.push_back( result.second );
        }
      }
      else if ( value == "proto" )
      {
        std::tie( name, commandData.returnType ) = readCommandProto( child );
      }
    }

    for ( auto & param : commandData.params )
    {
      for ( auto & lenParam : param.lenParams )
      {
        auto paramIt = findParamIt( lenParam.first, commandData.params );
        checkForError( paramIt != commandData.params.end(),
                       param.xmlLine,
                       "param <" + param.name + "> uses unknown len parameter <" + lenParam.first + "> in its \"altlen\" attribute <" + param.lenExpression +
                         ">" );
        lenParam.second = std::distance( commandData.params.cbegin(), paramIt );
      }
      if ( !param.strideParam.first.empty() )
      {
        auto paramIt = findParamIt( param.strideParam.first, commandData.params );
        checkForError(
          paramIt != commandData.params.end(), param.xmlLine, "param <" + param.name + "> uses unknown stride parameter <" + param.strideParam.first + ">" );
        param.strideParam.second = std::distance( commandData.params.cbegin(), paramIt );
      }
    }

    assert( !name.empty() );
    checkForError( ( commandData.returnType == "VkResult" ) || commandData.errorCodes.empty(),
                   line,
                   "command <" + name + "> does not return a VkResult but specifies errorcodes" );
    checkForError( ( commandData.returnType == "VkResult" ) || commandData.successCodes.empty(),
                   line,
                   "command <" + name + "> does not return a VkResult but specifies successcodes" );

    if ( api.empty() || ( api == m_api ) )
    {
      checkForError( m_commands.find( name ) == m_commands.end(), line, "command <" + name + "> already specified" );
      addCommand( name, commandData );
    }
  }
}

std::pair<bool, VulkanHppGenerator::ParamData> VulkanHppGenerator::readCommandParam( tinyxml2::XMLElement const *   element,
                                                                                     std::vector<ParamData> const & params )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line,
                   attributes,
                   {},
                   { { "altlen", {} },
                     { "api", { "vulkan", "vulkansc" } },
                     { "externsync", {} },
                     { "len", {} },
                     { "noautovalidity", { "true" } },
                     { "objecttype", { "objectType" } },
                     { "optional", { "false", "true" } },
                     { "stride", {} },
                     { "validstructs", {} } } );

  ParamData paramData;
  paramData.xmlLine = line;
  std::string api;
  for ( auto attribute : attributes )
  {
    if ( attribute.first == "altlen" )
    {
      assert( paramData.lenExpression.empty() );
      paramData.lenExpression = attribute.second;
      paramData.lenParams     = filterNumbers( tokenizeAny( attribute.second, " /()+" ) );
    }
    else if ( attribute.first == "api" )
    {
      api = attribute.second;
    }
    else if ( attribute.first == "len" )
    {
      if ( paramData.lenExpression.empty() )
      {
        paramData.lenExpression = attribute.second;
        auto paramIt            = findParamIt( attribute.second, params );
        if ( paramIt != params.end() )
        {
          paramData.lenParams.push_back( { attribute.second, std::distance( params.cbegin(), paramIt ) } );
        }
        else
        {
          checkForError( ( attribute.second == "null-terminated" ) || isLenByStructMember( attribute.second, params ),
                         line,
                         "attribute <len> holds an unknown value <" + attribute.second + ">" );
        }
      }
    }
    else if ( attribute.first == "stride" )
    {
      paramData.strideParam.first = attribute.second;
    }
    else if ( attribute.first == "optional" )
    {
      paramData.optional = ( attribute.second == "true" );
    }
    else if ( attribute.first == "validstructs" )
    {
      std::vector<std::string> validStructs = tokenize( attribute.second, "," );
      for ( auto const & vs : validStructs )
      {
        checkForError( m_structs.find( vs ) != m_structs.end(), line, "unknown struct <" + vs + "> listed in attribute <validstructs>" );
      }
    }
  }

  NameData nameData;
  std::tie( nameData, paramData.type ) = readNameAndType( element );

  checkForError( m_types.find( paramData.type.type ) != m_types.end(), line, "unknown type <" + paramData.type.type + ">" );
  checkForError( paramData.type.prefix.empty() || ( paramData.type.prefix == "const" ) || ( paramData.type.prefix == "const struct" ) ||
                   ( paramData.type.prefix == "struct" ),
                 line,
                 "unexpected type prefix <" + paramData.type.prefix + ">" );
  checkForError( paramData.type.postfix.empty() || ( paramData.type.postfix == "*" ) || ( paramData.type.postfix == "**" ) ||
                   ( paramData.type.postfix == "* const *" ),
                 line,
                 "unexpected type postfix <" + paramData.type.postfix + ">" );
  paramData.name       = nameData.name;
  paramData.arraySizes = nameData.arraySizes;

  if ( api.empty() || ( api == m_api ) )
  {
    checkForError( std::find_if( params.begin(), params.end(), [&name = nameData.name]( ParamData const & pd ) { return pd.name == name; } ) == params.end(),
                   line,
                   "command param <" + nameData.name + "> already used" );
  }
  return std::make_pair( api.empty() || ( api == m_api ), paramData );
}

std::pair<std::string, std::string> VulkanHppGenerator::readCommandProto( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), {}, {} );

  auto [nameData, typeInfo] = readNameAndType( element );

  checkForError( nameData.name.starts_with( "vk" ), line, "name <" + nameData.name + "> does not begin with <vk>" );
  checkForError( nameData.arraySizes.empty(), line, "name <" + nameData.name + "> with unsupported arraySizes" );
  checkForError( m_types.find( typeInfo.type ) != m_types.end(), line, "unknown type <" + typeInfo.type + ">" );
  checkForError( typeInfo.prefix.empty(), line, "unexpected type prefix <" + typeInfo.prefix + ">" );
  checkForError( typeInfo.postfix.empty(), line, "unexpected type postfix <" + typeInfo.postfix + ">" );

  return std::make_pair( nameData.name, typeInfo.type );
}

void VulkanHppGenerator::readCommands( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), {}, { { "comment", {} } } );

  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "command", false } } );
  for ( auto child : children )
  {
    readCommand( child );
  }
}

void VulkanHppGenerator::readEnums( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "name", {} } }, { { "bitwidth", { "64" } }, { "comment", {} }, { "type", { "bitmask", "enum" } } } );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );

  std::string bitwidth, name, type;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "bitwidth" )
    {
      bitwidth = attribute.second;
    }
    else if ( attribute.first == "name" )
    {
      name = attribute.second;
    }
    else if ( attribute.first == "type" )
    {
      type = attribute.second;
    }
  }
  assert( !name.empty() );

  if ( name == "API Constants" )
  {
    checkElements( line, children, { { "enum", false } }, {} );
    for ( auto const & child : children )
    {
      readEnumsConstants( child );
    }
  }
  else
  {
    checkElements( line, children, {}, { "comment", "enum", "unused" } );
    checkForError( !type.empty(), line, "enum without type" );

    // get the EnumData entry in enum map
    auto enumIt = m_enums.find( name );
    if ( enumIt == m_enums.end() )
    {
      auto aliasIt = m_enumAliases.find( name );
      if ( aliasIt != m_enumAliases.end() )
      {
        enumIt = m_enums.find( aliasIt->second.name );
      }
    }
    checkForError( enumIt != m_enums.end(), line, "enum <" + name + "> is not listed as enum in the types section" );
    checkForError( enumIt->second.values.empty(), line, "enum <" + name + "> already holds values" );

    // mark it as a bitmask, if it is one
    bool bitmask = ( type == "bitmask" );
    if ( bitmask )
    {
      checkForError( name.find( "FlagBits" ) != std::string::npos, line, "bitmask <" + name + "> does not contain \"FlagBits\"" );
    }
    enumIt->second.isBitmask = bitmask;
    enumIt->second.bitwidth  = bitwidth;

    // read the names of the enum values
    for ( auto child : children )
    {
      std::string value = child->Value();
      if ( value == "enum" )
      {
        readEnumsEnum( child, enumIt );
      }
    }
  }
}

void VulkanHppGenerator::readEnumsConstants( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkElements( line, getChildElements( element ), {} );

  auto aliasIt = attributes.find( "alias" );
  if ( aliasIt != attributes.end() )
  {
    checkAttributes( line, attributes, { { "alias", {} }, { "name", {} } }, {} );

    std::string alias = aliasIt->second;
    std::string name  = attributes.find( "name" )->second;

    checkForError( m_constants.find( alias ) != m_constants.end(), line, "enum <" + name + "> is an alias of an unknown enum <" + alias + ">." );
    checkForError( m_types.insert( { name, TypeData{ TypeCategory::Constant, {}, line } } ).second, line, "enum <" + name + "> already specified" );
    assert( m_constantAliases.find( name ) == m_constantAliases.end() );
    m_constantAliases[name] = { alias, line };
  }
  else
  {
    checkAttributes( line, attributes, { { "name", {} }, { "type", {} }, { "value", {} } }, { { "comment", {} } } );

    std::string alias, name, type, value;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "name" )
      {
        name = attribute.second;
        checkForError( m_constants.find( name ) == m_constants.end(), line, "already specified enum constant <" + name + ">" );
      }
      else if ( attribute.first == "type" )
      {
        type = attribute.second;
      }
      else if ( attribute.first == "value" )
      {
        value = attribute.second;
      }
    }

    checkForError( m_types.insert( { name, TypeData{ TypeCategory::Constant, {}, line } } ).second, line, "enum <" + name + "> already specified" );
    assert( m_constants.find( name ) == m_constants.end() );
    m_constants[name] = { type, value, line };
  }
}

void VulkanHppGenerator::readEnumsEnum( tinyxml2::XMLElement const * element, std::map<std::string, EnumData>::iterator enumIt )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  if ( attributes.find( "alias" ) != attributes.end() )
  {
    checkAttributes(
      line, attributes, { { "alias", {} }, { "name", {} } }, { { "api", { "vulkan", "vulkansc" } }, { "comment", {} }, { "deprecated", { "aliased" } } } );
    checkElements( line, getChildElements( element ), {} );

    std::string alias, api, bitpos, name, value;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "alias" )
      {
        alias = attribute.second;
      }
      else if ( attribute.first == "api" )
      {
        api = attribute.second;
      }
      else if ( attribute.first == "deprecated" )
      {
        // the enum value is marked as deprecated/aliased but still exisits -> no modifications needed here
      }
      else if ( attribute.first == "name" )
      {
        name = attribute.second;
      }
    }
    assert( !name.empty() );

    if ( api.empty() || ( api == m_api ) )
    {
      enumIt->second.addEnumAlias( line, name, alias, "", true );
    }
  }
  else
  {
    checkAttributes( line, attributes, { { "name", {} } }, { { "bitpos", {} }, { "comment", {} }, { "value", {} } } );
    checkElements( line, getChildElements( element ), {} );

    std::string alias, bitpos, name, value;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "bitpos" )
      {
        bitpos = attribute.second;
      }
      else if ( attribute.first == "name" )
      {
        name = attribute.second;
      }
      else if ( attribute.first == "value" )
      {
        value = attribute.second;
      }
    }

    std::string prefix = generateEnumSuffixes( enumIt->first, enumIt->second.isBitmask ).first;
    checkForError( name.starts_with( prefix ), line, "encountered enum value <" + name + "> that does not begin with expected prefix <" + prefix + ">" );

    checkForError( bitpos.empty() ^ value.empty(), line, "both or none of \"bitpos\" and \"value\" are set for enum <" + name + "> which is invalid" );
    enumIt->second.addEnumValue( line, name, "", !bitpos.empty(), true );
  }
}

std::string VulkanHppGenerator::readComment( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), {}, {} );
  checkElements( line, getChildElements( element ), {} );

  return element->GetText();
}

void VulkanHppGenerator::readExtensionRequire( tinyxml2::XMLElement const * element, ExtensionData & extensionData, bool extensionSupported )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, {}, { { "api", { "vulkansc" } }, { "comment", {} }, { "depends", {} } } );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, {}, { "command", "comment", "enum", "type" } );

  RequireData requireData{ .xmlLine = line };
  std::string api;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "api" )
    {
      api = attribute.second;
    }
    else if ( attribute.first == "depends" )
    {
      assert( requireData.depends.empty() );
      requireData.depends = tokenizeAny( attribute.second, ",+" );
      for ( auto const & d : requireData.depends )
      {
        checkForError( std::find_if( extensionData.requireData.begin(),
                                     extensionData.requireData.end(),
                                     [&d]( RequireData const & rd )
                                     { return std::find( rd.depends.begin(), rd.depends.end(), d ) != rd.depends.end(); } ) == extensionData.requireData.end(),
                       line,
                       "required extension <" + d + "> already listed" );
      }
    }
  }

  bool requireSupported = api.empty() || ( api == m_api );
  for ( auto child : children )
  {
    std::string value = child->Value();
    if ( value == "command" )
    {
      requireData.commands.push_back( readRequireCommand( child, extensionData.name ) );
    }
    else if ( value == "enum" )
    {
      readRequireEnum( child, extensionData.name, extensionData.platform, extensionSupported && requireSupported );
    }
    else if ( value == "type" )
    {
      requireData.types.push_back( readRequireType( child, extensionData.name ) );
    }
  }
  if ( requireSupported && ( !requireData.commands.empty() || !requireData.types.empty() ) )
  {
    extensionData.requireData.push_back( requireData );
  }
}

void VulkanHppGenerator::readExtensions( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), { { "comment", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "extension", false } } );

  for ( auto child : children )
  {
    readExtension( child );
  }
}

void VulkanHppGenerator::readExtension( tinyxml2::XMLElement const * element )
{
  int                                       line       = element->GetLineNum();
  std::map<std::string, std::string>        attributes = getAttributes( element );
  std::vector<tinyxml2::XMLElement const *> children   = getChildElements( element );

  checkAttributes( line,
                   attributes,
                   { { "name", {} }, { "number", {} }, { "supported", { "disabled", "vulkan", "vulkansc" } } },
                   { { "author", {} },
                     { "comment", {} },
                     { "contact", {} },
                     { "depends", {} },
                     { "deprecatedby", {} },
                     { "obsoletedby", {} },
                     { "platform", {} },
                     { "promotedto", {} },
                     { "provisional", { "true" } },
                     { "sortorder", { "1" } },
                     { "specialuse", { "cadsupport", "d3demulation", "debugging", "devtools", "glemulation" } },
                     { "type", { "device", "instance" } } } );
  checkElements( line, children, { { "require", false } } );

  ExtensionData            extensionData{ .xmlLine = line };
  std::vector<std::string> supported;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "depends" )
    {
      // we don't care about the logical implications of ',' and '+' here, we're just interested to get the depends strings
      extensionData.depends = tokenizeAny( attribute.second, ",+()" );
    }
    else if ( attribute.first == "deprecatedby" )
    {
      extensionData.deprecatedBy = attribute.second;
      extensionData.isDeprecated = true;
    }
    else if ( attribute.first == "name" )
    {
      extensionData.name = attribute.second;
      checkForError( !isExtension( extensionData.name ), line, "already encountered extension <" + extensionData.name + ">" );
    }
    else if ( attribute.first == "number" )
    {
      extensionData.number = attribute.second;
    }
    else if ( attribute.first == "obsoletedby" )
    {
      extensionData.obsoletedBy = attribute.second;
    }
    else if ( attribute.first == "platform" )
    {
      extensionData.platform = attribute.second;
      checkForError( m_platforms.find( extensionData.platform ) != m_platforms.end(), line, "unknown platform <" + extensionData.platform + ">" );
    }
    else if ( attribute.first == "promotedto" )
    {
      extensionData.promotedTo = attribute.second;
    }
    else if ( attribute.first == "provisional" )
    {
      if ( extensionData.platform.empty() )
      {
        // for now, having the attribute provisional="true" implies attribute platform="provisional" to get
        // stuff protected by VK_ENABLE_BETA_EXTENSIONS
        extensionData.platform = "provisional";
      }
      checkForError( extensionData.platform == "provisional",
                     line,
                     "while attribute <provisional> is set to \"true\", attribute <platform> is not set to \"provisional\" but to \"" + extensionData.platform +
                       "\"" );
    }
    else if ( attribute.first == "supported" )
    {
      supported = tokenize( attribute.second, "," );
    }
    else if ( attribute.first == "type" )
    {
      extensionData.type = attribute.second;
    }
  }

  bool extensionSupported = supported.empty() || ( std::find( supported.begin(), supported.end(), m_api ) != supported.end() );
  checkForError( !extensionSupported || !extensionData.type.empty(), line, "missing attribute \"type\" for supported extension <" + extensionData.name + ">" );
  for ( auto child : children )
  {
    readExtensionRequire( child, extensionData, extensionSupported );
  }

  if ( std::find( supported.begin(), supported.end(), "disabled" ) == supported.end() )
  {
    // extract the tag from the name, which is supposed to look like VK_<tag>_<other>
    size_t tagStart = extensionData.name.find( '_' );
    checkForError( tagStart != std::string::npos, line, "name <" + extensionData.name + "> is missing an underscore '_'" );
    size_t tagEnd = extensionData.name.find( '_', tagStart + 1 );
    checkForError( tagEnd != std::string::npos, line, "name <" + extensionData.name + "> is missing an underscore '_'" );
    std::string tag = extensionData.name.substr( tagStart + 1, tagEnd - tagStart - 1 );
    checkForError( ( m_tags.find( tag ) != m_tags.end() ), line, "name <" + extensionData.name + "> is using an unknown tag <" + tag + ">" );
  }

  if ( extensionSupported )
  {
    m_extensions.push_back( extensionData );
  }
  else
  {
    checkForError( m_unsupportedExtensions.insert( extensionData.name ).second, line, "unsupported extension <" + extensionData.name + "> already specified" );
  }
}

void VulkanHppGenerator::readFeature( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "api", { "vulkan", "vulkansc" } }, { "comment", {} }, { "name", {} }, { "number", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "require", false } }, { "remove" } );

  FeatureData featureData;
  featureData.xmlLine = line;
  std::vector<std::string> api;
  std::string              modifiedNumber;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "api" )
    {
      api = tokenize( attribute.second, "," );
    }
    else if ( attribute.first == "name" )
    {
      featureData.name = attribute.second;
    }
    else if ( attribute.first == "number" )
    {
      featureData.number = attribute.second;
      modifiedNumber     = featureData.number;
      std::replace( modifiedNumber.begin(), modifiedNumber.end(), '.', '_' );
    }
  }

  bool featureSupported = std::find( api.begin(), api.end(), m_api ) != api.end();
  for ( auto child : children )
  {
    std::string value = child->Value();
    if ( value == "remove" )
    {
      featureData.removeData.push_back( readFeatureRemove( child ) );
    }
    else if ( value == "require" )
    {
      featureData.requireData.push_back( readFeatureRequire( child, featureData.name, featureSupported ) );
    }
  }

  checkForError( featureData.name == ( ( std::find( api.begin(), api.end(), "vulkan" ) != api.end() ) ? "VK_VERSION_" : "VKSC_VERSION_" ) + modifiedNumber,
                 line,
                 "unexpected formatting of name <" + featureData.name + ">" );
  checkForError( !isFeature( featureData.name ), line, "feature <" + featureData.name + "> already specified" );
  if ( featureSupported )
  {
    m_features.push_back( featureData );
  }
  else
  {
    m_unsupportedFeatures.insert( featureData.name );
  }
}

VulkanHppGenerator::RemoveData VulkanHppGenerator::readFeatureRemove( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), {}, { { "comment", {} } } );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, {}, { "command", "enum", "type" } );

  RemoveData removeData;
  removeData.xmlLine = line;
  for ( auto child : children )
  {
    std::string value = child->Value();
    if ( value == "command" )
    {
      removeData.commands.push_back( readName( child ) );
    }
    else if ( value == "enum" )
    {
      removeData.enums.push_back( readName( child ) );
    }
    else if ( value == "type" )
    {
      removeData.types.push_back( readName( child ) );
    }
  }
  return removeData;
}

VulkanHppGenerator::RequireData
  VulkanHppGenerator::readFeatureRequire( tinyxml2::XMLElement const * element, std::string const & featureName, bool featureSupported )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), {}, { { "comment", {} } } );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, {}, { "command", "comment", "enum", "type" } );

  RequireData requireData{ .xmlLine = line };
  for ( auto child : children )
  {
    std::string value = child->Value();
    if ( value == "command" )
    {
      requireData.commands.push_back( readRequireCommand( child, featureName ) );
    }
    else if ( value == "enum" )
    {
      readRequireEnum( child, featureName, "", featureSupported );
    }
    else if ( value == "type" )
    {
      requireData.types.push_back( readRequireType( child, featureName ) );
    }
  }
  return requireData;
}

void VulkanHppGenerator::readFormat( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line,
                   attributes,
                   { { "blockSize", { "1", "2", "3", "4", "5", "6", "8", "12", "16", "24", "32" } },
                     { "class", {} },
                     { "name", {} },
                     { "texelsPerBlock", { "1", "16", "20", "25", "30", "36", "40", "48", "50", "60", "64", "80", "100", "120", "144" } } },
                   { { "blockExtent", { "1", "2", "4", "5", "6", "8", "10", "12" } },
                     { "chroma", { "420", "422", "444" } },
                     { "compressed", { "ASTC HDR", "ASTC LDR", "BC", "EAC", "ETC", "ETC2", "PVRTC" } },
                     { "packed", { "8", "16", "32" } } } );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "component", false } }, { "plane", "spirvimageformat" } );

  FormatData format;
  format.xmlLine = line;
  std::string name;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "blockExtent" )
    {
      checkForError( tokenize( attribute.second, "," ).size() == 3, line, "unexpected number of elements in attribute <blockExtent>" );
      format.blockExtent = attribute.second;
    }
    if ( attribute.first == "blockSize" )
    {
      format.blockSize = attribute.second;
    }
    else if ( attribute.first == "chroma" )
    {
      format.chroma = attribute.second;
    }
    else if ( attribute.first == "class" )
    {
      format.classAttribute = attribute.second;
    }
    else if ( attribute.first == "compressed" )
    {
      format.compressed = attribute.second;
    }
    else if ( attribute.first == "name" )
    {
      name = attribute.second;
    }
    else if ( attribute.first == "packed" )
    {
      format.packed = attribute.second;
    }
    else if ( attribute.first == "texelsPerBlock" )
    {
      format.texelsPerBlock = attribute.second;
    }
  }

  for ( auto child : children )
  {
    std::string value = child->Value();
    if ( value == "component" )
    {
      readFormatComponent( child, format );
    }
    else if ( value == "plane" )
    {
      readFormatPlane( child, format );
    }
    else if ( value == "spirvimageformat" )
    {
      readFormatSPIRVImageFormat( child, format );
    }
  }

  if ( format.components.front().bits == "compressed" )
  {
    for ( auto componentIt = std::next( format.components.begin() ); componentIt != format.components.end(); ++componentIt )
    {
      checkForError( componentIt->bits == "compressed", line, "component is expected to be marked as compressed in attribute <bits>" );
    }
  }
  if ( !format.components.front().planeIndex.empty() )
  {
    for ( auto componentIt = std::next( format.components.begin() ); componentIt != format.components.end(); ++componentIt )
    {
      checkForError( !componentIt->planeIndex.empty(), line, "component is expected to have a planeIndex" );
    }
    size_t planeCount = 1 + std::stoi( format.components.back().planeIndex );
    checkForError( format.planes.size() == planeCount, line, "number of planes does not fit to largest planeIndex of the components" );
  }

  auto formatIt = m_enums.find( "VkFormat" );
  assert( formatIt != m_enums.end() );

  auto valueIt =
    std::find_if( formatIt->second.values.begin(), formatIt->second.values.end(), [&name]( EnumValueData const & evd ) { return evd.name == name; } );
  if ( valueIt != formatIt->second.values.end() )
  {
    checkForError( m_formats.insert( { name, format } ).second, line, "format <" + name + "> already specified" );
  }
  else
  {
    checkForError( std::find_if( formatIt->second.unsupportedValues.begin(),
                                 formatIt->second.unsupportedValues.end(),
                                 [&name]( EnumValueData const & evd ) { return evd.name == name; } ) != formatIt->second.unsupportedValues.end(),
                   line,
                   "unknown format <" + name + ">" );
  }
}

void VulkanHppGenerator::readFormatComponent( tinyxml2::XMLElement const * element, FormatData & formatData )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line,
                   attributes,
                   { { "bits", { "1", "2", "4", "5", "6", "8", "9", "10", "11", "12", "16", "24", "32", "64", "compressed" } },
                     { "name", {} },
                     { "numericFormat", { "SFLOAT", "SINT", "SNORM", "SRGB", "SSCALED", "UFLOAT", "UINT", "UNORM", "USCALED" } } },
                   { { "planeIndex", { "0", "1", "2" } } } );
  checkElements( line, getChildElements( element ), {} );

  formatData.components.emplace_back();
  ComponentData & component = formatData.components.back();
  component.xmlLine         = line;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "bits" )
    {
      checkForError(
        ( attribute.second != "compressed" ) || !formatData.compressed.empty(), line, "component of a not compressed format is marked as compressed" );
      component.bits = attribute.second;
    }
    else if ( attribute.first == "name" )
    {
      component.name = attribute.second;
    }
    else if ( attribute.first == "numericFormat" )
    {
      component.numericFormat = attribute.second;
    }
    else if ( attribute.first == "planeIndex" )
    {
      component.planeIndex = attribute.second;
    }
  }
}

void VulkanHppGenerator::readFormatPlane( tinyxml2::XMLElement const * element, FormatData & formatData )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes(
    line, attributes, { { "compatible", {} }, { "index", { "0", "1", "2" } }, { "heightDivisor", { "1", "2" } }, { "widthDivisor", { "1", "2" } } }, {} );
  checkElements( line, getChildElements( element ), {} );

  formatData.planes.emplace_back();
  PlaneData & plane = formatData.planes.back();
  plane.xmlLine     = line;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "compatible" )
    {
      plane.compatible = attribute.second;
      auto formatIt    = m_enums.find( "VkFormat" );
      assert( formatIt != m_enums.end() );
      checkForError( std::find_if( formatIt->second.values.begin(),
                                   formatIt->second.values.end(),
                                   [&plane]( EnumValueData const & evd ) { return evd.name == plane.compatible; } ) != formatIt->second.values.end(),
                     line,
                     "encountered unknown format <" + plane.compatible + ">" );
    }
    else if ( attribute.first == "index" )
    {
      size_t index = std::stoi( attribute.second );
      checkForError( index + 1 == formatData.planes.size(), line, "unexpected index <" + attribute.second + ">" );
    }
    else if ( attribute.first == "heightDivisor" )
    {
      plane.heightDivisor = attribute.second;
    }
    else if ( attribute.first == "widthDivisor" )
    {
      plane.widthDivisor = attribute.second;
    }
  }
}

void VulkanHppGenerator::readFormats( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), {}, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "format", false } } );

  for ( auto child : children )
  {
    readFormat( child );
  }
}

void VulkanHppGenerator::readFormatSPIRVImageFormat( tinyxml2::XMLElement const * element, FormatData & formatData )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "name", {} } }, {} );
  checkElements( line, getChildElements( element ), {} );

  std::string name = attributes.find( "name" )->second;

  checkForError( formatData.spirvImageFormat.empty(), line, "spirvimageformat <" + name + "> already specified" );
  formatData.spirvImageFormat = name;
}

std::string VulkanHppGenerator::readName( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "name", {} } }, {} );
  checkElements( line, getChildElements( element ), {} );

  return attributes.find( "name" )->second;
}

std::pair<VulkanHppGenerator::NameData, VulkanHppGenerator::TypeInfo> VulkanHppGenerator::readNameAndType( tinyxml2::XMLElement const * element )
{
  int                                       line     = element->GetLineNum();
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "name", true } }, { { "enum" }, { "type" } } );

  NameData nameData;
  TypeInfo typeInfo;
  for ( auto child : children )
  {
    line = child->GetLineNum();
    checkAttributes( line, getAttributes( child ), {}, {} );
    checkElements( line, getChildElements( child ), {} );

    std::string value = child->Value();
    if ( value == "enum" )
    {
      nameData.arraySizes.push_back( child->GetText() );
      checkForError( child->PreviousSibling() && ( strcmp( child->PreviousSibling()->Value(), "[" ) == 0 ) && child->NextSibling() &&
                       ( strcmp( child->NextSibling()->Value(), "]" ) == 0 ),
                     line,
                     std::string( "array specifiation is ill-formatted: <" ) + nameData.arraySizes.back() + ">" );
      checkForError(
        m_constants.find( nameData.arraySizes.back() ) != m_constants.end(), line, "using unknown enum value <" + nameData.arraySizes.back() + ">" );
    }
    else if ( value == "name" )
    {
      nameData.name = child->GetText();
      std::string bitCount;
      std::tie( nameData.arraySizes, bitCount ) = readModifiers( child->NextSibling() );
      checkForError( bitCount.empty(), line, "name <" + nameData.name + "> with unsupported bitCount <" + bitCount + ">" );
    }
    else if ( value == "type" )
    {
      typeInfo = readTypeInfo( child );
    }
  }
  return std::make_pair( nameData, typeInfo );
}

void VulkanHppGenerator::readPlatform( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "comment", {} }, { "name", {} }, { "protect", {} } }, {} );
  checkElements( line, getChildElements( element ), {} );

  std::string name, protect;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "name" )
    {
      name = attribute.second;
    }
    else if ( attribute.first == "protect" )
    {
      protect = attribute.second;
    }
  }

  checkForError( std::find_if( m_platforms.begin(),
                               m_platforms.end(),
                               [&protect]( std::pair<std::string, PlatformData> const & p ) { return p.second.protect == protect; } ) == m_platforms.end(),
                 line,
                 "protect <" + protect + "> of platform <" + name + "> is already used with some other platform" );
  checkForError( m_platforms.insert( { name, { protect, line } } ).second, line, "platform <" + name + "> is already specified" );
}

void VulkanHppGenerator::readPlatforms( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), { { "comment", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "platform", false } } );

  for ( auto child : children )
  {
    readPlatform( child );
  }
}

void VulkanHppGenerator::readRegistry( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), {}, {} );

  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line,
                 children,
                 { { "commands", true },
                   { "comment", false },
                   { "enums", false },
                   { "extensions", true },
                   { "feature", false },
                   { "formats", true },
                   { "platforms", true },
                   { "spirvcapabilities", true },
                   { "spirvextensions", true },
                   { "tags", true },
                   { "types", true } } );
  for ( auto child : children )
  {
    const std::string value = child->Value();
    if ( value == "commands" )
    {
      readCommands( child );
    }
    else if ( value == "comment" )
    {
      std::string comment = readComment( child );
      if ( comment.find( "\nCopyright" ) == 0 )
      {
        setVulkanLicenseHeader( child->GetLineNum(), comment );
      }
    }
    else if ( value == "enums" )
    {
      readEnums( child );
    }
    else if ( value == "extensions" )
    {
      readExtensions( child );
    }
    else if ( value == "feature" )
    {
      readFeature( child );
    }
    else if ( value == "formats" )
    {
      readFormats( child );
    }
    else if ( value == "platforms" )
    {
      readPlatforms( child );
    }
    else if ( value == "spirvcapabilities" )
    {
      readSPIRVCapabilities( child );
    }
    else if ( value == "spirvextensions" )
    {
      readSPIRVExtensions( child );
    }
    else if ( value == "tags" )
    {
      readTags( child );
    }
    else if ( value == "types" )
    {
      readTypes( child );
    }
  }
}

std::string VulkanHppGenerator::readRequireCommand( tinyxml2::XMLElement const * element, std::string const & requiredBy )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "name", {} } }, { { "comment", {} } } );
  checkElements( line, getChildElements( element ), {} );

  std::string name      = attributes.find( "name" )->second;
  auto        commandIt = m_commands.find( name );
  if ( commandIt == m_commands.end() )
  {
    auto aliasIt = m_commandAliases.find( name );
    checkForError( aliasIt != m_commandAliases.end(), line, "unknown required command <" + name + ">" );
    commandIt = m_commands.find( aliasIt->second.name );
    assert( commandIt != m_commands.end() );
  }
  commandIt->second.requiredBy.insert( requiredBy );

  return name;
}

void VulkanHppGenerator::readRequireEnum( tinyxml2::XMLElement const * element, std::string const & requiredBy, std::string const & platform, bool supported )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkElements( line, getChildElements( element ), {} );

  if ( attributes.find( "alias" ) == attributes.end() )
  {
    checkAttributes( line,
                     attributes,
                     { { "name", {} } },
                     { { "api", { "vulkan", "vulkansc" } },
                       { "bitpos", {} },
                       { "comment", {} },
                       { "dir", { "-" } },
                       { "extends", {} },
                       { "extnumber", {} },
                       { "offset", {} },
                       { "protect", { "VK_ENABLE_BETA_EXTENSIONS" } },
                       { "value", {} } } );

    std::string api, bitpos, extends, name, offset, protect, value;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "api" )
      {
        api = attribute.second;
      }
      else if ( attribute.first == "bitpos" )
      {
        bitpos = attribute.second;
      }
      if ( attribute.first == "extends" )
      {
        extends = attribute.second;
      }
      else if ( attribute.first == "name" )
      {
        name = attribute.second;
      }
      else if ( attribute.first == "offset" )
      {
        offset = attribute.second;
      }
      else if ( attribute.first == "protect" )
      {
        protect = attribute.second;
      }
      else if ( attribute.first == "value" )
      {
        value = attribute.second;
      }
    }
    if ( protect.empty() )
    {
      protect = getProtectFromPlatform( platform );
    }

    if ( extends.empty() )
    {
      if ( value.empty() )
      {
        auto typeIt = m_types.find( name );
        checkForError( typeIt != m_types.end(), line, "unknown required enum <" + name + ">" );
        typeIt->second.requiredBy.insert( requiredBy );
      }
      else
      {
        if ( api.empty() || ( api == m_api ) )
        {
          checkForError( m_types.insert( { name, TypeData{ TypeCategory::Constant, { requiredBy }, line } } ).second,
                         line,
                         "required enum <" + name + "> specified by value <" + value + "> is already specified" );
        }
      }
    }
    else
    {
      checkForError( bitpos.empty() + offset.empty() + value.empty() == 2,
                     line,
                     "exactly one out of bitpos = <" + bitpos + ">, offset = <" + offset + ">, and value = <" + value + "> are supposed to be empty" );
      auto typeIt = m_types.find( extends );
      checkForError( typeIt != m_types.end(), line, "enum value <" + name + "> extends unknown type <" + extends + ">" );
      checkForError( typeIt->second.category == TypeCategory::Enum, line, "enum value <" + name + "> extends non-enum type <" + extends + ">" );
      typeIt->second.requiredBy.insert( requiredBy );
      auto enumIt = m_enums.find( extends );
      assert( enumIt != m_enums.end() );

      enumIt->second.addEnumValue( line, name, protect, !bitpos.empty(), ( api.empty() || ( api == m_api ) ) && supported );
    }
  }
  else
  {
    checkAttributes( line,
                     attributes,
                     { { "alias", {} }, { "name", {} } },
                     { { "api", { "vulkan", "vulkansc" } }, { "comment", {} }, { "deprecated", { "aliased" } }, { "extends", {} } } );

    std::string alias, api, extends, name;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "alias" )
      {
        alias = attribute.second;
      }
      else if ( attribute.first == "api" )
      {
        api = attribute.second;
      }
      else if ( attribute.first == "extends" )
      {
        extends = attribute.second;
      }
      else if ( attribute.first == "name" )
      {
        name = attribute.second;
      }
    }

    if ( extends.empty() )
    {
      checkForError(
        m_types.insert( { name, TypeData{ TypeCategory::Constant, { requiredBy }, line } } ).second, line, "required enum <" + name + "> already specified" );
      assert( m_constantAliases.find( name ) == m_constantAliases.end() );
      m_constantAliases[name] = { alias, line };
    }
    else
    {
      auto typeIt = m_types.find( extends );
      checkForError( typeIt != m_types.end(), line, "enum value <" + name + "> extends unknown type <" + extends + ">" );
      checkForError( typeIt->second.category == TypeCategory::Enum, line, "enum value <" + name + "> extends non-enum type <" + extends + ">" );
      typeIt->second.requiredBy.insert( requiredBy );

      auto enumIt = m_enums.find( extends );
      assert( enumIt != m_enums.end() );
      enumIt->second.addEnumAlias( line, name, alias, getProtectFromPlatform( platform ), ( api.empty() || ( api == m_api ) ) && supported );
    }
  }
}

std::string VulkanHppGenerator::readRequireType( tinyxml2::XMLElement const * element, std::string const & requiredBy )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "name", {} } }, { { "comment", {} } } );
  checkElements( line, getChildElements( element ), {} );

  std::string name   = attributes.find( "name" )->second;
  auto        typeIt = m_types.find( name );
  checkForError( typeIt != m_types.end(), line, "unknown required type <" + name + ">" );
  typeIt->second.requiredBy.insert( requiredBy );

  return name;
}

void VulkanHppGenerator::readSPIRVCapability( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "name", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, {}, { "enable" } );

  for ( auto child : children )
  {
    readSPIRVCapabilityEnable( child );
  }
}

void VulkanHppGenerator::readSPIRVCapabilityEnable( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkElements( line, getChildElements( element ), {}, {} );

  if ( attributes.find( "extension" ) != attributes.end() )
  {
    checkAttributes( line, attributes, { { "extension", {} } }, {} );

    std::string const & extension = attributes.find( "extension" )->second;
    checkForError( isExtension( extension ), line, "unknown extension <" + extension + "> specified for SPIR-V capability" );
  }
  else if ( attributes.find( "property" ) != attributes.end() )
  {
    checkAttributes( line, attributes, { { "member", {} }, { "property", {} }, { "requires", {} }, { "value", {} } }, {} );

    std::string member, property, value;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "member" )
      {
        member = attribute.second;
      }
      else if ( attribute.first == "property" )
      {
        property = attribute.second;
      }
      if ( attribute.first == "requires" )
      {
        std::vector<std::string> require = tokenize( attribute.second, "," );
        for ( auto const & r : require )
        {
          checkForError( isFeature( r ) || isExtension( r ), line, "unknown requires <" + r + "> specified for SPIR-V capability" );
        }
      }
      else if ( attribute.first == "value" )
      {
        value = attribute.second;
      }
    }

    auto propertyIt = m_structs.find( property );
    checkForError( propertyIt != m_structs.end(), line, "unknown property <" + property + "> specified for SPIR-V capability" );
    auto memberIt = findStructMemberIt( member, propertyIt->second.members );
    checkForError( memberIt != propertyIt->second.members.end(), line, "unknown member <" + member + "> specified for SPIR-V capability" );
    if ( memberIt->type.type == "VkBool32" )
    {
      checkForError( ( value == "VK_FALSE" ) || ( value == "VK_TRUE" ),
                     line,
                     "unknown value <" + value + "> for boolean member <" + member + "> specified for SPIR-V capability" );
    }
    else
    {
      auto bitmaskIt = m_bitmasks.find( memberIt->type.type );
      checkForError( bitmaskIt != m_bitmasks.end(), line, "member <" + member + "> specified for SPIR-V capability is not a bitmask" );
      checkForError( !bitmaskIt->second.require.empty(), line, "member <" + member + "> specified for SPIR-V capability has no required enum" );
      auto enumIt = m_enums.find( bitmaskIt->second.require );
      checkForError(
        enumIt != m_enums.end(), line, "member <" + member + "> specified for SPIR-V capability requires an unknown enum <" + bitmaskIt->second.require + ">" );
      checkForError(
        ( std::find_if( enumIt->second.values.begin(), enumIt->second.values.end(), [&value]( EnumValueData const & evd ) { return evd.name == value; } ) !=
          enumIt->second.values.end() ) ||
          ( std::find_if( enumIt->second.unsupportedValues.begin(),
                          enumIt->second.unsupportedValues.end(),
                          [&value]( EnumValueData const & evd ) { return evd.name == value; } ) != enumIt->second.unsupportedValues.end() ),
        line,
        "unknown attribute value <" + value + "> specified for SPIR-V capability" );
    }
  }
  else if ( attributes.find( "struct" ) != attributes.end() )
  {
    checkAttributes( line, attributes, { { "feature", {} }, { "struct", {} } }, { { "alias", {} }, { "requires", {} } } );

    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "requires" )
      {
        std::vector<std::string> require = tokenize( attribute.second, "," );
        for ( auto const & r : require )
        {
          checkForError( isFeature( r ) || isExtension( r ), line, "unknown requires <" + r + "> specified for SPIR-V capability" );
        }
      }
      else if ( attribute.first == "struct" )
      {
        checkForError( ( m_structs.find( attribute.second ) != m_structs.end() ) || ( m_structAliases.find( attribute.second ) != m_structAliases.end() ),
                       line,
                       "unknown structure <" + attribute.second + "> specified for SPIR-V capability" );
      }
    }
  }
  else if ( attributes.find( "version" ) != attributes.end() )
  {
    checkAttributes( line, attributes, { { "version", {} } }, {} );

    std::string version = attributes.find( "version" )->second;
    if ( version.starts_with( "VK_API_" ) )
    {
      version.erase( 3, 4 );  // remove "API_" from the version -> VK_VERSION_x_y
    }
    checkForError( isFeature( version ), line, "unknown version <" + version + "> specified for SPIR-V capability" );
  }
}

void VulkanHppGenerator::readSPIRVCapabilities( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "comment", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, {}, { "spirvcapability" } );

  for ( auto child : children )
  {
    readSPIRVCapability( child );
  }
}

void VulkanHppGenerator::readSPIRVExtension( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "name", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, {}, { "enable" } );

  for ( auto child : children )
  {
    readSPIRVExtensionEnable( child );
  }
}

void VulkanHppGenerator::readSPIRVExtensionEnable( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, {}, { { "extension", {} }, { "version", {} } } );
  checkElements( line, getChildElements( element ), {}, {} );

  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "extension" )
    {
      checkForError( isExtension( attribute.second ), line, "unknown extension <" + attribute.second + "> specified for SPIR-V extension" );
    }
    else
    {
      assert( attribute.first == "version" );
      std::string feature = attribute.second;
      if ( feature.starts_with( "VK_API_" ) )
      {
        feature.erase( 3, 4 );  // remove "API_" from the version -> VK_VERSION_x_y
      }
      checkForError( isFeature( feature ), line, "unknown version <" + attribute.second + "> specified for SPIR-V extension" );
    }
  }
}

void VulkanHppGenerator::readSPIRVExtensions( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "comment", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, {}, { "spirvextension" } );

  for ( auto child : children )
  {
    readSPIRVExtension( child );
  }
}

void VulkanHppGenerator::readStructMember( tinyxml2::XMLElement const * element, std::vector<MemberData> & members, bool isUnion )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line,
                   attributes,
                   {},
                   { { "altlen", {} },
                     { "api", { "vulkan", "vulkansc" } },
                     { "deprecated", { "ignored" } },
                     { "externsync", { "true" } },
                     { "len", {} },
                     { "limittype", { "bitmask", "bits", "exact", "max", "min", "mul", "noauto", "pot", "range", "struct" } },
                     { "noautovalidity", { "true" } },
                     { "objecttype", { "objectType" } },
                     { "optional", { "false", "true" } },
                     { "selection", {} },
                     { "selector", {} },
                     { "values", {} } } );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "name", true }, { "type", true } }, { "comment", "enum" } );

  MemberData memberData;
  memberData.xmlLine = line;

  std::string api;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "api" )
    {
      api = attribute.second;
    }
    else if ( attribute.first == "altlen" )
    {
      assert( memberData.lenExpressions.empty() );
      memberData.lenExpressions = tokenize( attribute.second, "," );
      checkForError( memberData.lenExpressions.size() == 1,
                     line,
                     "member attribute <altlen> holds unknown number of data: " + std::to_string( memberData.lenExpressions.size() ) );
      memberData.lenMembers = filterNumbers( tokenizeAny( attribute.second, " /()+*" ) );
    }
    else if ( attribute.second == "deprecated" )
    {
      assert( false );
      // the struct member is marked as deprecated/ignored, but still exisits -> no modifications needed here
    }
    else if ( attribute.first == "len" )
    {
      if ( memberData.lenExpressions.empty() )
      {
        memberData.lenExpressions = tokenize( attribute.second, "," );
        checkForError( !memberData.lenExpressions.empty() && ( memberData.lenExpressions.size() <= 2 ),
                       line,
                       "member attribute <len> holds unknown number of data: " + std::to_string( memberData.lenExpressions.size() ) );
        if ( memberData.lenExpressions[0] != "null-terminated" )
        {
          auto lenMemberIt = findStructMemberIt( memberData.lenExpressions[0], members );
          checkForError( lenMemberIt != members.end(), line, "member attribute <len> holds unknown value <" + memberData.lenExpressions[0] + ">" );
          checkForError( lenMemberIt->type.prefix.empty(),
                         line,
                         "member attribute <len> references a member of unexpected type <" + lenMemberIt->type.compose( "VULKAN_HPP_NAMESPACE" ) + ">" );
          memberData.lenMembers.push_back( { memberData.lenExpressions[0], std::distance( members.cbegin(), lenMemberIt ) } );
        }
        if ( 1 < memberData.lenExpressions.size() )
        {
          checkForError( ( memberData.lenExpressions[1] == "1" ) || ( memberData.lenExpressions[1] == "null-terminated" ),
                         line,
                         "member attribute <len> holds unknown second value <" + memberData.lenExpressions[1] + ">" );
        }
      }
    }
    else if ( attribute.first == "noautovalidity" )
    {
      memberData.noAutoValidity = ( attribute.second == "true" );
    }
    else if ( attribute.first == "optional" )
    {
      std::vector<std::string> optional = tokenize( attribute.second, "," );
      memberData.optional.reserve( optional.size() );
      for ( auto const & o : optional )
      {
        memberData.optional.push_back( o == "true" );
      }
    }
    else if ( attribute.first == "selection" )
    {
      checkForError( isUnion, line, "attribute <selection> is used with a non-union structure." );
      memberData.selection = tokenize( attribute.second, "," );
    }
    else if ( attribute.first == "selector" )
    {
      memberData.selector = attribute.second;
      auto selectorIt     = findStructMemberIt( memberData.selector, members );
      checkForError( selectorIt != members.end(), line, "member attribute <selector> holds unknown value <" + memberData.selector + ">" );
      checkForError( m_enums.find( selectorIt->type.type ) != m_enums.end(),
                     line,
                     "member attribute <selector> references unknown enum type <" + selectorIt->type.type + ">" );
    }
    else if ( attribute.first == "values" )
    {
      std::vector<std::string> values = tokenize( attribute.second, "," );
      checkForError( values.size() == 1, line, "attribute \"values\" holds multiple values <" + attribute.first + ">, but it's expected to hold just one" );
      memberData.value = values[0];
    }
  }

  std::string name;
  for ( auto child : children )
  {
    int childLine = child->GetLineNum();
    checkAttributes( childLine, getAttributes( child ), {}, {} );
    checkElements( childLine, getChildElements( child ), {}, {} );

    std::string value = child->Value();
    if ( value == "enum" )
    {
      std::string enumString = child->GetText();

      checkForError( child->PreviousSibling() && ( strcmp( child->PreviousSibling()->Value(), "[" ) == 0 ) && child->NextSibling() &&
                       ( strcmp( child->NextSibling()->Value(), "]" ) == 0 ),
                     line,
                     std::string( "struct member array specifiation is ill-formatted: <" ) + enumString + ">" );

      memberData.arraySizes.push_back( enumString );
      checkForError( memberData.usedConstant.empty(), line, "struct already holds a constant <" + memberData.usedConstant + ">" );
      memberData.usedConstant = enumString;
    }
    else if ( value == "name" )
    {
      name                                                   = child->GetText();
      std::tie( memberData.arraySizes, memberData.bitCount ) = readModifiers( child->NextSibling() );
    }
    else if ( value == "type" )
    {
      memberData.type = readTypeInfo( child );
    }
  }
  assert( !name.empty() );

  if ( api.empty() || ( api == m_api ) )
  {
    checkForError( std::find_if( members.begin(), members.end(), [&name]( MemberData const & md ) { return md.name == name; } ) == members.end(),
                   line,
                   "struct member name <" + name + "> already used" );
    memberData.name = name;
    members.push_back( memberData );
  }
}

void VulkanHppGenerator::readTag( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );
  checkAttributes( line, attributes, { { "author", {} }, { "contact", {} }, { "name", {} } }, {} );
  checkElements( line, getChildElements( element ), {} );

  std::string name = attributes.find( "name" )->second;
  checkForError( m_tags.insert( { name, { line } } ).second, line, "tag <" + name + "> already specified" );
}

void VulkanHppGenerator::readTags( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), { { "comment", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "tag", false } } );

  for ( auto child : children )
  {
    readTag( child );
  }
}

void VulkanHppGenerator::readTypeBasetype( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();
  checkAttributes( line, attributes, { { "category", { "basetype" } } }, {} );

  NameData nameData;
  TypeInfo typeInfo;
  std::tie( nameData, typeInfo ) = readNameAndType( element );

  if ( typeInfo.prefix == "typedef" )
  {
    // remove redundant typeInfo.prefix "typedef"
    typeInfo.prefix.clear();
  }

  checkForError( nameData.arraySizes.empty(), line, "name <" + nameData.name + "> with unsupported arraySizes" );
  checkForError( typeInfo.prefix.empty(), line, "unexpected type prefix <" + typeInfo.prefix + ">" );
  checkForError( typeInfo.postfix.empty() || ( typeInfo.postfix == "*" ), line, "unexpected type postfix <" + typeInfo.postfix + ">" );

  checkForError(
    m_types.insert( { nameData.name, TypeData{ TypeCategory::BaseType, {}, line } } ).second, line, "basetype <" + nameData.name + "> already specified" );
  assert( m_baseTypes.find( nameData.name ) == m_baseTypes.end() );
  m_baseTypes[nameData.name] = { typeInfo, line };
}

void VulkanHppGenerator::readTypeBitmask( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();

  auto aliasIt = attributes.find( "alias" );
  if ( aliasIt != attributes.end() )
  {
    checkAttributes( line, attributes, { { "alias", {} }, { "category", { "bitmask" } }, { "name", {} } }, {} );
    checkElements( line, getChildElements( element ), {} );

    std::string alias = aliasIt->second;
    std::string name  = attributes.find( "name" )->second;

    checkForError( m_bitmasks.find( alias ) != m_bitmasks.end(), line, "bitmask <" + name + "> is an alias of an unknown bitmask <" + alias + ">." );
    checkForError( m_types.insert( { name, TypeData{ TypeCategory::Bitmask, {}, line } } ).second, line, "bitmask <" + name + "> already specified" );
    assert( m_bitmaskAliases.find( name ) == m_bitmaskAliases.end() );
    m_bitmaskAliases[name] = { alias, line };
  }
  else
  {
    checkAttributes( line, attributes, { { "category", { "bitmask" } } }, { { "api", { "vulkan", "vulkansc" } }, { "bitvalues", {} }, { "requires", {} } } );

    std::string api, bitvalues, require;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "api" )
      {
        api = attribute.second;
      }
      else if ( attribute.first == "bitvalues" )
      {
        bitvalues = attribute.second;
      }
      else if ( attribute.first == "requires" )
      {
        require = attribute.second;
      }
    }

    NameData nameData;
    TypeInfo typeInfo;
    std::tie( nameData, typeInfo ) = readNameAndType( element );

    checkForError( nameData.name.starts_with( "Vk" ), line, "name <" + nameData.name + "> does not begin with <Vk>" );
    checkForError( nameData.arraySizes.empty(), line, "name <" + nameData.name + "> with unsupported arraySizes" );
    checkForWarning( ( typeInfo.type == "VkFlags" ) || ( typeInfo.type == "VkFlags64" ), line, "unexpected bitmask type <" + typeInfo.type + ">" );
    checkForError( typeInfo.prefix == "typedef", line, "unexpected type prefix <" + typeInfo.prefix + ">" );
    checkForError( typeInfo.postfix.empty(), line, "unexpected type postfix <" + typeInfo.postfix + ">" );
    checkForError( bitvalues.empty() || require.empty(), line, "attributes <bitvalues> and <requires> are both specified" );
    checkForError( ( typeInfo.type != "VkFlags64" ) || !bitvalues.empty(), line, "bitmask of type <VkFlags64> needs attribute bitvalues to be set" );

    if ( !bitvalues.empty() )
    {
      require = bitvalues;
    }

    if ( api.empty() || ( api == m_api ) )
    {
      checkForError(
        m_types.insert( { nameData.name, TypeData{ TypeCategory::Bitmask, {}, line } } ).second, line, "bitmask <" + nameData.name + "> already specified" );
      assert( m_bitmasks.find( nameData.name ) == m_bitmasks.end() );
      m_bitmasks[nameData.name] = { require, typeInfo.type, line };
    }
  }
}

void VulkanHppGenerator::readTypeDefine( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();
  checkAttributes( line,
                   attributes,
                   { { "category", { "define" } } },
                   { { "api", { "vulkan", "vulkansc" } }, { "comment", {} }, { "deprecated", { "true" } }, { "name", {} }, { "requires", {} } } );

  std::string api, name, require;
  bool        deprecated = false;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "api" )
    {
      api = attribute.second;
    }
    else if ( attribute.first == "deprecated" )
    {
      assert( attribute.second == "true" );
      deprecated = true;
    }
    else if ( attribute.first == "name" )
    {
      name = attribute.second;
    }
    else if ( attribute.first == "requires" )
    {
      require = attribute.second;
    }
  }

  if ( !name.empty() )
  {
    checkForError( !element->FirstChildElement(), line, "unknown formatting of type category=define name <" + name + ">" );
    checkForError( element->LastChild() && element->LastChild()->ToText() && element->LastChild()->ToText()->Value(),
                   line,
                   "unknown formatting of type category=define named <" + name + ">" );

    // filter out the check for the different types of VK_DEFINE_NON_DISPATCHABLE_HANDLE
    if ( name == "VK_USE_64_BIT_PTR_DEFINES" )
    {
      m_typesafeCheck = "#if ( VK_USE_64_BIT_PTR_DEFINES == 1 )";
    }
    else if ( ( name == "VK_DEFINE_NON_DISPATCHABLE_HANDLE" ) && ( m_typesafeCheck.empty() ) )
    {
      assert( false );
      std::string text  = element->LastChild()->ToText()->Value();
      size_t      start = text.find( "#if defined(__LP64__)" );
      checkForError( start != std::string::npos, line, "unexpected text in type category=define named <" + name + ">" );
      size_t end = text.find_first_of( "\r\n", start + 1 );
      checkForError( end != std::string::npos, line, "unexpected text in type category=define named <" + name + ">" );
      m_typesafeCheck = text.substr( start, end - start );
    }
  }
  else if ( element->GetText() )
  {
    std::string text = element->GetText();
    if ( ( text.find( "class" ) != std::string::npos ) || ( text.find( "struct" ) != std::string::npos ) )
    {
      // here are a couple of structs as defines, which really are types!
      assert( false );
      tinyxml2::XMLElement const * child = element->FirstChildElement();
      checkForError( child && ( strcmp( child->Value(), "name" ) == 0 ) && child->GetText(), line, "unexpected formatting of type category=define" );
      name = child->GetText();
      checkForError(
        m_types.insert( std::make_pair( name, TypeData{ .category = TypeCategory::Define } ) ).second, line, "type <" + name + "> has already been speficied" );
    }
    else
    {
      tinyxml2::XMLElement const * child = element->FirstChildElement();
      checkForError( child && !child->FirstAttribute() && ( strcmp( child->Value(), "name" ) == 0 ) && child->GetText(),
                     line,
                     "unknown formatting of type category define" );
      name = trim( child->GetText() );
      if ( ( name == "VK_HEADER_VERSION" ) && ( api.empty() || ( api == m_api ) ) )
      {
        m_version = trimEnd( element->LastChild()->ToText()->Value() );
      }
      // ignore all the other defines
      checkForWarning( !child->NextSiblingElement() ||
                         ( child->NextSiblingElement() && !child->NextSiblingElement()->FirstAttribute() &&
                           ( strcmp( child->NextSiblingElement()->Value(), "type" ) == 0 ) && !child->NextSiblingElement()->NextSiblingElement() ),
                       line,
                       "unknown formatting of type category define" );
    }
  }
  assert( !name.empty() );

  if ( api.empty() || ( api == m_api ) )
  {
    checkForError( m_types.insert( { name, TypeData{ TypeCategory::Define, {}, line } } ).second, line, "define <" + name + "> already specified" );
    assert( m_defines.find( name ) == m_defines.end() );
    m_defines[name] = { deprecated, require, line };
  }
}

void VulkanHppGenerator::readTypeEnum( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();
  checkAttributes( line, attributes, { { "category", { "enum" } }, { "name", {} } }, { { "alias", {} } } );
  checkElements( line, getChildElements( element ), {} );

  std::string alias, name;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "alias" )
    {
      alias = attribute.second;
    }
    else if ( attribute.first == "name" )
    {
      name = attribute.second;
    }
  }

  checkForError( m_types.insert( { name, TypeData{ TypeCategory::Enum, {}, line } } ).second, line, "enum <" + name + "> already specified" );
  if ( alias.empty() )
  {
    assert( m_enums.find( name ) == m_enums.end() );
    m_enums[name] = EnumData{ .xmlLine = line };
  }
  else
  {
    checkForError( m_enums.find( alias ) != m_enums.end(), line, "enum <" + name + "> uses unknown alias <" + alias + ">" );
    assert( m_enumAliases.find( name ) == m_enumAliases.end() );
    m_enumAliases[name] = { alias, line };
  }
}

void VulkanHppGenerator::readTypeFuncpointer( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();
  checkAttributes( line, attributes, { { "category", { "funcpointer" } } }, { { "requires", {} } } );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "name", true } }, { "type" } );

  std::string require;
  for ( auto const & attribute : attributes )
  {
    if ( attribute.first == "requires" )
    {
      require = attribute.second;
    }
  }

  std::string                          name;
  std::vector<FuncPointerArgumentData> arguments;
  for ( auto const & child : children )
  {
    std::string value = child->Value();
    if ( value == "name" )
    {
      name = child->GetText();
    }
    else if ( value == "type" )
    {
      int         argumentLine = child->GetLineNum();
      std::string type         = child->GetText();

      auto         sibling      = child->NextSibling();
      char const * siblingValue = sibling->Value();
      assert( siblingValue != nullptr );
      std::string argumentName = siblingValue;
      argumentName             = argumentName.substr( argumentName.find_first_not_of( "* " ) );
      argumentName             = argumentName.substr( 0, argumentName.find_first_of( ",)" ) );

      checkForError( std::find_if( arguments.begin(),
                                   arguments.end(),
                                   [&argumentName]( FuncPointerArgumentData const & argument ) { return argument.name == argumentName; } ) == arguments.end(),
                     line,
                     "argument <" + argumentName + "> already listed in funcpointer <" + name + ">" );
      arguments.push_back( { argumentName, type, argumentLine } );
    }
  }
  assert( !name.empty() );

  std::set<std::string> argumentNames;
  checkForError( m_types.insert( { name, TypeData{ TypeCategory::FuncPointer, {}, line } } ).second, line, "funcpointer <" + name + "> already specified" );
  assert( m_funcPointers.find( name ) == m_funcPointers.end() );
  m_funcPointers[name] = { arguments, require, line };
}

void VulkanHppGenerator::readTypeHandle( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();

  auto aliasIt = attributes.find( "alias" );
  if ( aliasIt != attributes.end() )
  {
    checkAttributes( line, attributes, { { "alias", {} }, { "category", { "handle" } }, { "name", {} } }, {} );
    checkElements( line, getChildElements( element ), {} );

    std::string alias = aliasIt->second;
    std::string name  = attributes.find( "name" )->second;

    checkForError( m_handles.find( alias ) != m_handles.end(), line, "handle <" + name + "> uses unknown alias <" + alias + ">." );
    checkForError( m_types.insert( { name, TypeData{ TypeCategory::Handle, {}, line } } ).second, line, "handle <" + name + "> already specified" );
    assert( m_handleAliases.find( name ) == m_handleAliases.end() );
    m_handleAliases[name] = { alias, line };
  }
  else
  {
    checkAttributes( line, attributes, { { "category", { "handle" } }, { "objtypeenum", {} } }, { { "parent", {} } } );

    HandleData handleData;
    handleData.xmlLine = line;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "objtypeenum" )
      {
        handleData.objTypeEnum = attribute.second;
      }
      else if ( attribute.first == "parent" )
      {
        handleData.parent = attribute.second;
      }
    }

    NameData nameData;
    TypeInfo typeInfo;
    std::tie( nameData, typeInfo ) = readNameAndType( element );
    handleData.isDispatchable      = typeInfo.type == "VK_DEFINE_HANDLE";

    checkForError( nameData.name.starts_with( "Vk" ), line, "name <" + nameData.name + "> does not begin with <Vk>" );
    checkForError( nameData.arraySizes.empty(), line, "name <" + nameData.name + "> with unsupported arraySizes" );
    checkForError( ( typeInfo.type == "VK_DEFINE_HANDLE" ) || ( typeInfo.type == "VK_DEFINE_NON_DISPATCHABLE_HANDLE" ),
                   line,
                   "handle with invalid type <" + typeInfo.type + ">" );
    checkForError( typeInfo.prefix.empty(), line, "unexpected type prefix <" + typeInfo.prefix + ">" );
    checkForError( typeInfo.postfix == "(", line, "unexpected type postfix <" + typeInfo.postfix + ">" );
    checkForError( !handleData.objTypeEnum.empty(), line, "handle <" + nameData.name + "> does not specify attribute \"objtypeenum\"" );

    checkForError(
      m_types.insert( { nameData.name, TypeData{ TypeCategory::Handle, {}, line } } ).second, line, "handle <" + nameData.name + "> already specified" );
    assert( m_handles.find( nameData.name ) == m_handles.end() );
    m_handles.insert( { nameData.name, handleData } );
  }
}

void VulkanHppGenerator::readTypeInclude( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();
  checkAttributes( line, attributes, { { "category", { "include" } }, { "name", {} } }, {} );
  checkElements( line, getChildElements( element ), {} );

  std::string name = attributes.find( "name" )->second;
  checkForError( m_types.insert( { name, TypeData{ TypeCategory::Include, {}, line } } ).second, line, "type <" + name + "> already specified" );
  assert( m_includes.find( name ) == m_includes.end() );
  m_includes[name] = { line };
}

void VulkanHppGenerator::readTypeRequires( tinyxml2::XMLElement const * element, std::map<std::string, std::string> const & attributes )
{
  int line = element->GetLineNum();
  checkAttributes( line, attributes, { { "name", {} }, { "requires", {} } }, {} );
  checkElements( line, getChildElements( element ), {} );

  std::string name, require;
  for ( auto attribute : attributes )
  {
    if ( attribute.first == "name" )
    {
      name = attribute.second;
    }
    else
    {
      assert( attribute.first == "requires" );
      require = attribute.second;
    }
  }

  checkForError( m_includes.find( require ) != m_includes.end(), line, "type <" + name + "> requires unknown <" + require + ">" );
  checkForError( m_types.insert( { name, TypeData{ TypeCategory::ExternalType, {}, line } } ).second, line, "type <" + name + "> already specified" );
  assert( m_externalTypes.find( name ) == m_externalTypes.end() );
  m_externalTypes[name] = { require, line };
}

void VulkanHppGenerator::readTypeStruct( tinyxml2::XMLElement const * element, bool isUnion, std::map<std::string, std::string> const & attributes )
{
  int  line    = element->GetLineNum();
  auto aliasIt = attributes.find( "alias" );
  if ( aliasIt != attributes.end() )
  {
    checkAttributes( line, attributes, { { "alias", {} }, { "category", { "struct" } }, { "name", {} } }, {} );
    checkElements( line, getChildElements( element ), {}, {} );

    std::string alias = aliasIt->second;
    std::string name  = attributes.find( "name" )->second;

    checkForError( m_types.insert( { name, TypeData{ TypeCategory::Struct, {}, line } } ).second, line, "struct <" + name + "> already specified" );
    assert( m_structAliases.find( name ) == m_structAliases.end() );
    m_structAliases[name] = { alias, line };
  }

  else
  {
    checkAttributes( line,
                     attributes,
                     { { "category", { isUnion ? "union" : "struct" } }, { "name", {} } },
                     { { "allowduplicate", { "false", "true" } }, { "comment", {} }, { "returnedonly", { "true" } }, { "structextends", {} } } );
    std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
    checkElements( line, children, {}, { "member", "comment" } );

    StructureData structureData;
    structureData.xmlLine = line;
    structureData.isUnion = isUnion;

    std::string name;
    for ( auto const & attribute : attributes )
    {
      if ( attribute.first == "allowduplicate" )
      {
        structureData.allowDuplicate = ( attribute.second == "true" );
      }
      else if ( attribute.first == "category" )
      {
        assert( isUnion ? ( attribute.second == "union" ) : ( attribute.second == "struct" ) );
      }
      else if ( attribute.first == "name" )
      {
        name = attribute.second;
      }
      else if ( attribute.first == "returnedonly" )
      {
        assert( attribute.second == "true" );
        structureData.returnedOnly = true;
      }
      else if ( attribute.first == "structextends" )
      {
        structureData.structExtends = tokenize( attribute.second, "," );
      }
    }
    assert( !name.empty() );

    checkForError( !structureData.allowDuplicate || !structureData.structExtends.empty(),
                   line,
                   "attribute <allowduplicate> is true, but no structures are listed in <structextends>" );

    checkForError( m_types.insert( { name, TypeData{ isUnion ? TypeCategory::Union : TypeCategory::Struct, {}, line } } ).second,
                   line,
                   "struct <" + name + "> already specified" );
    assert( m_structs.find( name ) == m_structs.end() );

    std::map<std::string, StructureData>::iterator it = m_structs.insert( std::make_pair( name, structureData ) ).first;

    for ( auto child : children )
    {
      std::string value = child->Value();
      if ( value == "member" )
      {
        readStructMember( child, it->second.members, isUnion );
      }
    }
    it->second.subStruct = determineSubStruct( *it );

    // check if multiple structure members use the very same (not empty) len attribute
    // Note: even though the arrays are not marked as optional, they still might be mutually exclusive (like in
    // VkWriteDescriptorSet)! That is, there's not enough information available in vk.xml to decide on that, so we
    // need this external knowledge!
    static std::set<std::string> mutualExclusiveStructs = {
      "VkAccelerationStructureBuildGeometryInfoKHR", "VkAccelerationStructureTrianglesOpacityMicromapEXT", "VkMicromapBuildInfoEXT", "VkWriteDescriptorSet"
    };
    static std::set<std::string> multipleLenStructs = { "VkAccelerationStructureTrianglesDisplacementMicromapNV",
                                                        "VkImageConstraintsInfoFUCHSIA",
                                                        "VkIndirectCommandsLayoutTokenNV",
                                                        "VkPresentInfoKHR",
                                                        "VkSemaphoreWaitInfo",
                                                        "VkSubmitInfo",
                                                        "VkSubpassDescription",
                                                        "VkSubpassDescription2",
                                                        "VkWin32KeyedMutexAcquireReleaseInfoKHR",
                                                        "VkWin32KeyedMutexAcquireReleaseInfoNV" };
    bool                         warned             = false;
    for ( auto m0It = it->second.members.begin(); !warned && ( m0It != it->second.members.end() ); ++m0It )
    {
      if ( !m0It->lenExpressions.empty() && ( m0It->lenExpressions.front() != "null-terminated" ) )
      {
        for ( auto m1It = std::next( m0It ); !warned && ( m1It != it->second.members.end() ); ++m1It )
        {
          if ( !m1It->lenExpressions.empty() && ( m0It->lenExpressions.front() == m1It->lenExpressions.front() ) )
          {
            if ( mutualExclusiveStructs.find( it->first ) != mutualExclusiveStructs.end() )
            {
              it->second.mutualExclusiveLens = true;
            }
            else
            {
              checkForWarning(
                multipleLenStructs.find( it->first ) != multipleLenStructs.end(),
                line,
                "Encountered structure <" + it->first +
                  "> with multiple members referencing the same member for len. Need to be checked if they are supposed to be mutually exclusive." );
              warned = true;
            }
          }
        }
      }
    }

    m_extendedStructs.insert( structureData.structExtends.begin(), structureData.structExtends.end() );
  }
}

void VulkanHppGenerator::readTypes( tinyxml2::XMLElement const * element )
{
  int line = element->GetLineNum();
  checkAttributes( line, getAttributes( element ), { { "comment", {} } }, {} );
  std::vector<tinyxml2::XMLElement const *> children = getChildElements( element );
  checkElements( line, children, { { "comment", false }, { "type", false } } );

  for ( auto child : children )
  {
    std::string value = child->Value();
    if ( value == "type" )
    {
      readTypesType( child );
    }
  }
}

void VulkanHppGenerator::readTypesType( tinyxml2::XMLElement const * element )
{
  int                                line       = element->GetLineNum();
  std::map<std::string, std::string> attributes = getAttributes( element );

  auto categoryIt = attributes.find( "category" );
  if ( categoryIt != attributes.end() )
  {
    if ( categoryIt->second == "basetype" )
    {
      readTypeBasetype( element, attributes );
    }
    else if ( categoryIt->second == "bitmask" )
    {
      readTypeBitmask( element, attributes );
    }
    else if ( categoryIt->second == "define" )
    {
      readTypeDefine( element, attributes );
    }
    else if ( categoryIt->second == "enum" )
    {
      readTypeEnum( element, attributes );
    }
    else if ( categoryIt->second == "funcpointer" )
    {
      readTypeFuncpointer( element, attributes );
    }
    else if ( categoryIt->second == "handle" )
    {
      readTypeHandle( element, attributes );
    }
    else if ( categoryIt->second == "include" )
    {
      readTypeInclude( element, attributes );
    }
    else if ( ( categoryIt->second == "struct" ) || ( categoryIt->second == "union" ) )
    {
      readTypeStruct( element, ( categoryIt->second == "union" ), attributes );
    }
    else
    {
      checkForError( false, line, "unknown category <" + categoryIt->second + "> encountered" );
    }
  }
  else
  {
    auto requiresIt = attributes.find( "requires" );
    if ( requiresIt != attributes.end() )
    {
      readTypeRequires( element, attributes );
    }
    else
    {
      checkForError( ( attributes.size() == 1 ) && ( attributes.begin()->first == "name" ) && ( attributes.begin()->second == "int" ), line, "unknown type" );
      checkForError( m_types.insert( { "int", TypeData{ TypeCategory::Unknown, {}, line } } ).second, line, "type <int> already specified" );
    }
  }
}

VulkanHppGenerator::TypeInfo VulkanHppGenerator::readTypeInfo( tinyxml2::XMLElement const * element ) const
{
  TypeInfo                  typeInfo;
  tinyxml2::XMLNode const * previousSibling = element->PreviousSibling();
  if ( previousSibling && previousSibling->ToText() )
  {
    typeInfo.prefix = trim( previousSibling->Value() );
  }
  typeInfo.type                         = element->GetText();
  tinyxml2::XMLNode const * nextSibling = element->NextSibling();
  if ( nextSibling && nextSibling->ToText() )
  {
    typeInfo.postfix = trimStars( trimEnd( nextSibling->Value() ) );
  }
  return typeInfo;
}

void VulkanHppGenerator::registerDeleter( std::string const & commandName, CommandData const & commandData )
{
  if ( ( commandName.substr( 2, 7 ) == "Destroy" ) || ( commandName.substr( 2, 4 ) == "Free" ) )
  {
    std::string key;
    size_t      valueIndex;
    switch ( commandData.params.size() )
    {
      case 2:
      case 3:
        assert( commandData.params.back().type.type == "VkAllocationCallbacks" );
        key        = ( commandData.params.size() == 2 ) ? "" : commandData.params[0].type.type;
        valueIndex = commandData.params.size() - 2;
        break;
      case 4:
        key        = commandData.params[0].type.type;
        valueIndex = 3;
        assert( m_handles.find( commandData.params[valueIndex].type.type ) != m_handles.end() );
        m_handles.find( commandData.params[valueIndex].type.type )->second.deletePool = commandData.params[1].type.type;
        break;
      default: assert( false ); valueIndex = 0;
    }
    auto keyHandleIt = m_handles.find( key );
    assert( keyHandleIt != m_handles.end() );
    keyHandleIt->second.childrenHandles.insert( commandData.params[valueIndex].type.type );

    auto handleIt = m_handles.find( commandData.params[valueIndex].type.type );
    assert( handleIt != m_handles.end() );
    handleIt->second.deleteCommand = commandName;
    handleIt->second.deleteParent  = key;
  }
}

void VulkanHppGenerator::rescheduleRAIIHandle( std::string &                              str,
                                               std::pair<std::string, HandleData> const & handle,
                                               std::set<std::string> &                    listedHandles,
                                               std::set<std::string> const &              specialFunctions ) const
{
  listedHandles.insert( handle.first );
  if ( !handle.second.parent.empty() && ( listedHandles.find( handle.second.parent ) == listedHandles.end() ) )
  {
    auto parentIt = m_handles.find( handle.second.parent );
    assert( parentIt != m_handles.end() );
    str += generateRAIIHandle( *parentIt, listedHandles, specialFunctions );
  }

  for ( auto constructorIt : handle.second.constructorIts )
  {
    for ( auto const & param : constructorIt->second.params )
    {
      auto handleIt = m_handles.find( param.type.type );
      if ( handleIt != m_handles.end() && ( listedHandles.find( param.type.type ) == listedHandles.end() ) )
      {
        str += generateRAIIHandle( *handleIt, listedHandles, specialFunctions );
      }
    }
  }
}

std::vector<std::string> VulkanHppGenerator::selectCommandsByHandle( std::vector<RequireData> const & requireData,
                                                                     std::set<std::string> const &    handleCommands,
                                                                     std::set<std::string> &          listedCommands ) const
{
  std::vector<std::string> selectedCommands;
  for ( auto const & require : requireData )
  {
    for ( auto const & command : require.commands )
    {
      if ( ( handleCommands.find( command ) != handleCommands.end() ) && listedCommands.insert( command ).second )
      {
        selectedCommands.push_back( command );
      }
    }
  }
  return selectedCommands;
}

void VulkanHppGenerator::setVulkanLicenseHeader( int line, std::string const & comment )
{
  checkForError( m_vulkanLicenseHeader.empty(), line, "second encounter of a Copyright comment" );
  m_vulkanLicenseHeader = comment;

  // replace any '\n' with "\n// "
  for ( size_t pos = m_vulkanLicenseHeader.find( '\n' ); pos != std::string::npos; pos = m_vulkanLicenseHeader.find( '\n', pos + 1 ) )
  {
    m_vulkanLicenseHeader.replace( pos, 1, "\n// " );
  }
  // remove any trailing spaces
  m_vulkanLicenseHeader = trimEnd( m_vulkanLicenseHeader );

  // and add a little message on our own
  m_vulkanLicenseHeader += "\n\n// This header is generated from the Khronos Vulkan XML API Registry.";
  m_vulkanLicenseHeader = trim( m_vulkanLicenseHeader ) + "\n";
}

bool VulkanHppGenerator::skipLeadingGrandParent( std::pair<std::string, HandleData> const & handle ) const
{
  bool skip = false;
  assert( !handle.second.constructorIts.empty() );
  auto constructorIt = handle.second.constructorIts.begin();
  if ( ( 1 < ( *constructorIt )->second.params.size() ) && isHandleType( ( *constructorIt )->second.params[0].type.type ) &&
       ( ( *constructorIt )->second.params[1].type.type == handle.second.parent ) )
  {
    auto parentIt = m_handles.find( handle.second.parent );
    assert( parentIt != m_handles.end() );
    skip = ( ( *constructorIt )->second.params[0].type.type == parentIt->second.parent );
#if !defined( NDEBUG )
    for ( auto it = std::next( constructorIt ); it != handle.second.constructorIts.end(); ++it )
    {
      assert( ( *it )->second.params[0].type.type == ( *constructorIt )->second.params[0].type.type );
      assert( !skip || ( ( *it )->second.params[1].type.type == ( *constructorIt )->second.params[1].type.type ) );
    }
#endif
  }
  return skip;
}

std::string VulkanHppGenerator::stripPluralS( std::string const & name ) const
{
  std::string strippedName = name;
  std::string tag          = findTag( name );
  if ( strippedName.ends_with( "s" + tag ) )
  {
    size_t pos = strippedName.rfind( 's' );
    if ( ( 2 <= pos ) && ( strippedName.substr( pos - 2, 3 ) == "ies" ) )
    {
      strippedName.replace( pos - 2, 3, "y" );
    }
    else
    {
      strippedName.erase( pos, 1 );
    }
  }
  return strippedName;
}

std::string VulkanHppGenerator::toString( TypeCategory category )
{
  switch ( category )
  {
    case TypeCategory::Bitmask: return "bitmask";
    case TypeCategory::BaseType: return "basetype";
    case TypeCategory::Define: return "define";
    case TypeCategory::Enum: return "enum";
    case TypeCategory::ExternalType: return "ExternalType";
    case TypeCategory::FuncPointer: return "funcpointer";
    case TypeCategory::Handle: return "handle";
    case TypeCategory::Struct: return "struct";
    case TypeCategory::Union: return "union";
    case TypeCategory::Unknown: return "unkown";
    default: assert( false ); return "";
  }
}

void VulkanHppGenerator::EnumData::addEnumAlias( int line, std::string const & name, std::string const & alias, std::string const & protect, bool supported )
{
  auto & valuesRef = supported ? values : unsupportedValues;
  auto   valueIt   = std::find_if( valuesRef.begin(), valuesRef.end(), [&name]( EnumValueData const & evd ) { return evd.name == name; } );
  if ( valueIt != valuesRef.end() )
  {
    if ( supported )
    {
      checkForError( ( valueIt->alias == alias ) && ( valueIt->protect == protect ) && !valueIt->singleBit,
                     line,
                     "enum alias <" + name + "> already specified with different attributes" );
    }
    else
    {
      checkForWarning( ( valueIt->alias == alias ) && ( valueIt->protect == protect ) && !valueIt->singleBit,
                       line,
                       "enum alias <" + name + "> already specified with different attributes" );
    }
  }
  else
  {
    valuesRef.push_back( { alias, name, protect, false, line } );
  }
}

void VulkanHppGenerator::EnumData::addEnumValue( int line, std::string const & name, std::string const & protect, bool singleBit, bool supported )
{
  auto & valuesRef = supported ? values : unsupportedValues;
  auto   valueIt   = std::find_if( valuesRef.begin(), valuesRef.end(), [&name]( EnumValueData const & evd ) { return evd.name == name; } );
  if ( valueIt != valuesRef.end() )
  {
    checkForError( valueIt->alias.empty() && ( valueIt->protect == protect ) && ( valueIt->singleBit == singleBit ),
                   line,
                   "enum value <" + name + "> already specified with different attributes" );
  }
  else
  {
    valuesRef.push_back( { "", name, protect, singleBit, line } );
  }
}

std::string VulkanHppGenerator::TypeInfo::compose( std::string const & nameSpace ) const
{
  return prefix + ( prefix.empty() ? "" : " " ) +
         ( nameSpace.empty() ? type : ( ( ( type.substr( 0, 2 ) == "Vk" ) ? ( nameSpace + "::" ) : "" ) + stripPrefix( type, "Vk" ) ) ) +
         ( postfix.empty() ? "" : " " ) + postfix;
}

//
// VulkanHppGenerator local functions
//

// check the validity of an attributes map
// line       : the line in the xml file where the attributes are listed
// attributes : the map of name/value pairs of the encountered attributes
// required   : the required attributes, with a set of allowed values per attribute
// optional   : the optional attributes, with a set of allowed values per attribute
void checkAttributes( int                                                  line,
                      std::map<std::string, std::string> const &           attributes,
                      std::map<std::string, std::set<std::string>> const & required,
                      std::map<std::string, std::set<std::string>> const & optional )
{
  // check if all required attributes are included and if there is a set of allowed values, check if the actual
  // value is part of that set
  for ( auto const & r : required )
  {
    auto attributesIt = attributes.find( r.first );
    checkForError( attributesIt != attributes.end(), line, "missing attribute <" + r.first + ">" );
    if ( !r.second.empty() )
    {
      std::vector<std::string> values = tokenize( attributesIt->second, "," );
      for ( auto const & v : values )
      {
        checkForError( r.second.find( v ) != r.second.end(), line, "unexpected attribute value <" + v + "> in attribute <" + attributesIt->first + ">" );
      }
    }
  }
  // check if all not required attributes or optional, and if there is a set of allowed values, check if the
  // actual value is part of that set
  for ( auto const & a : attributes )
  {
    if ( required.find( a.first ) == required.end() )
    {
      auto optionalIt = optional.find( a.first );
      if ( optionalIt == optional.end() )
      {
        checkForWarning( false, line, "unknown attribute <" + a.first + ">" );
        continue;
      }
      if ( !optionalIt->second.empty() )
      {
        std::vector<std::string> values = tokenize( a.second, "," );
        for ( auto const & v : values )
        {
          checkForWarning(
            optionalIt->second.find( v ) != optionalIt->second.end(), line, "unexpected attribute value <" + v + "> in attribute <" + a.first + ">" );
        }
      }
    }
  }
}

void checkElements( int                                               line,
                    std::vector<tinyxml2::XMLElement const *> const & elements,
                    std::map<std::string, bool> const &               required,
                    std::set<std::string> const &                     optional )
{
  std::map<std::string, size_t> encountered;
  for ( auto const & e : elements )
  {
    std::string value = e->Value();
    encountered[value]++;
    checkForWarning(
      ( required.find( value ) != required.end() ) || ( optional.find( value ) != optional.end() ), e->GetLineNum(), "unknown element <" + value + ">" );
  }
  for ( auto const & r : required )
  {
    auto encounteredIt = encountered.find( r.first );
    checkForError( encounteredIt != encountered.end(), line, "missing required element <" + r.first + ">" );
    // check: r.second (means: required excactly once) => (encouteredIt->second == 1)
    checkForError( !r.second || ( encounteredIt->second == 1 ),
                   line,
                   "required element <" + r.first + "> is supposed to be listed exactly once, but is listed " + std::to_string( encounteredIt->second ) );
  }
}

void checkForError( bool condition, int line, std::string const & message )
{
  if ( !condition )
  {
    throw std::runtime_error( "VulkanHppGenerator: Spec error on line " + std::to_string( line ) + ": " + message );
  }
}

void checkForWarning( bool condition, int line, std::string const & message )
{
  if ( !condition )
  {
    std::cerr << "VulkanHppGenerator: Spec warning on line " << std::to_string( line ) << ": " << message << "!" << std::endl;
  }
}

std::vector<std::pair<std::string, size_t>> filterNumbers( std::vector<std::string> const & names )
{
  std::vector<std::pair<std::string, size_t>> filteredNames;
  for ( auto const & name : names )
  {
    if ( !isNumber( name ) )
    {
      filteredNames.push_back( { name, ~0 } );
    }
  }
  return filteredNames;
}

std::string generateCArraySizes( std::vector<std::string> const & sizes )
{
  std::string arraySizes;
  for ( auto const & s : sizes )
  {
    arraySizes += "[" + s + "]";
  }
  return arraySizes;
}

std::string generateNamespacedType( std::string const & type )
{
  return type.starts_with( "Vk" ) ? ( "VULKAN_HPP_NAMESPACE::" + stripPrefix( type, "Vk" ) ) : type;
}

std::string generateNoDiscard( bool returnsSomething, bool multiSuccessCodes, bool multiErrorCodes )
{
  return ( returnsSomething || multiSuccessCodes ) ? "VULKAN_HPP_NODISCARD " : ( multiErrorCodes ? "VULKAN_HPP_NODISCARD_WHEN_NO_EXCEPTIONS " : "" );
}

std::string generateStandardArray( std::string const & type, std::vector<std::string> const & sizes )
{
  std::string arrayString = "std::array<" + type + "," + sizes.back() + ">";
  for ( size_t i = sizes.size() - 2; i < sizes.size(); i-- )
  {
    arrayString = "std::array<" + arrayString + "," + sizes[i] + ">";
  }
  return arrayString;
}

std::string generateStandardArrayWrapper( std::string const & type, std::vector<std::string> const & sizes )
{
  std::string arrayString = "VULKAN_HPP_NAMESPACE::ArrayWrapper" + std::to_string( sizes.size() ) + "D<" + type;
  for ( auto const & size : sizes )
  {
    arrayString += ", " + size;
  }
  arrayString += ">";
  return arrayString;
}

std::map<std::string, std::string> getAttributes( tinyxml2::XMLElement const * element )
{
  std::map<std::string, std::string> attributes;
  for ( auto attribute = element->FirstAttribute(); attribute; attribute = attribute->Next() )
  {
    assert( attributes.find( attribute->Name() ) == attributes.end() );
    attributes[attribute->Name()] = attribute->Value();
  }
  return attributes;
}

template <typename ElementContainer>
std::vector<tinyxml2::XMLElement const *> getChildElements( ElementContainer const * element )
{
  std::vector<tinyxml2::XMLElement const *> childElements;
  for ( tinyxml2::XMLElement const * childElement = element->FirstChildElement(); childElement; childElement = childElement->NextSiblingElement() )
  {
    childElements.push_back( childElement );
  }
  return childElements;
}

bool isNumber( std::string const & name )
{
  return name.find_first_not_of( "0123456789" ) == std::string::npos;
}

std::pair<std::vector<std::string>, std::string> readModifiers( tinyxml2::XMLNode const * node )
{
  std::vector<std::string> arraySizes;
  std::string              bitCount;
  if ( node && node->ToText() )
  {
    // following the name there might be some array size
    std::string value = node->Value();
    assert( !value.empty() );
    if ( value[0] == '[' )
    {
      std::string::size_type endPos = 0;
      while ( endPos + 1 != value.length() )
      {
        std::string::size_type startPos = value.find( '[', endPos );
        checkForError( startPos != std::string::npos, node->GetLineNum(), "could not find '[' in <" + value + ">" );
        endPos = value.find( ']', startPos );
        checkForError( endPos != std::string::npos, node->GetLineNum(), "could not find ']' in <" + value + ">" );
        checkForError( startPos + 2 <= endPos, node->GetLineNum(), "missing content between '[' and ']' in <" + value + ">" );
        arraySizes.push_back( value.substr( startPos + 1, endPos - startPos - 1 ) );
      }
    }
    else if ( value[0] == ':' )
    {
      bitCount = value.substr( 1 );
    }
    else
    {
      checkForError( ( value[0] == ';' ) || ( value[0] == ')' ), node->GetLineNum(), "unknown modifier <" + value + ">" );
    }
  }
  return std::make_pair( arraySizes, bitCount );
}

std::string readSnippet( std::string const & snippetFile )
{
  std::ifstream ifs( std::string( BASE_PATH ) + "/snippets/" + snippetFile );
  assert( !ifs.fail() );
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

std::string replaceWithMap( std::string const & input, std::map<std::string, std::string> replacements )
{
  // This will match ${someVariable} and contain someVariable in match group 1
  std::regex re( R"(\$\{([^\}]+)\})" );
  auto       it  = std::sregex_iterator( input.begin(), input.end(), re );
  auto       end = std::sregex_iterator();

  // No match, just return the original string
  if ( it == end )
  {
    assert( replacements.empty() );
    return input;
  }

#if !defined( NDEBUG )
  std::set<std::string> matchedReplacements;
#endif

  std::string result = "";
  while ( it != end )
  {
    std::smatch match         = *it;
    auto        itReplacement = replacements.find( match[1].str() );
    assert( itReplacement != replacements.end() );
#if !defined( NDEBUG )
    matchedReplacements.insert( match[1].str() );
#endif

    result += match.prefix().str() + ( ( itReplacement != replacements.end() ) ? itReplacement->second : match[0].str() );
    ++it;

    // we've passed the last match. Append the rest of the orignal string
    if ( it == end )
    {
      result += match.suffix().str();
    }
  }
#if !defined( NDEBUG )
  std::set<std::string> missedReplacements;
  for ( auto r : replacements )
  {
    if ( matchedReplacements.find( r.first ) == matchedReplacements.end() )
    {
      missedReplacements.insert( r.first );
    }
  }
  assert( missedReplacements.empty() );
#endif
  return result;
}

std::string startLowerCase( std::string const & input )
{
  assert( !input.empty() );
  return static_cast<char>( tolower( input[0] ) ) + input.substr( 1 );
}

std::string startUpperCase( std::string const & input )
{
  assert( !input.empty() );
  return static_cast<char>( toupper( input[0] ) ) + input.substr( 1 );
}

std::string stripPostfix( std::string const & value, std::string const & postfix )
{
  std::string strippedValue = value;
  if ( strippedValue.ends_with( postfix ) )
  {
    strippedValue.erase( strippedValue.length() - postfix.length() );
  }
  return strippedValue;
}

std::string stripPrefix( std::string const & value, std::string const & prefix )
{
  std::string strippedValue = value;
  if ( strippedValue.starts_with( prefix ) )
  {
    strippedValue.erase( 0, prefix.length() );
  }
  return strippedValue;
}

std::string toCamelCase( std::string const & value )
{
  assert( !value.empty() && ( isupper( value[0] ) || isdigit( value[0] ) ) );
  std::string result;
  result.reserve( value.size() );
  bool keepUpper = true;
  for ( auto c : value )
  {
    if ( c == '_' )
    {
      keepUpper = true;
    }
    else if ( isdigit( c ) )
    {
      keepUpper = true;
      result.push_back( c );
    }
    else if ( keepUpper )
    {
      result.push_back( c );
      keepUpper = false;
    }
    else
    {
      result.push_back( static_cast<char>( tolower( c ) ) );
    }
  }
  return result;
}

std::string toUpperCase( std::string const & name )
{
  std::string convertedName;
  bool        previousIsLowerCase = false;
  bool        previousIsDigit     = false;
  for ( auto c : name )
  {
    if ( ( isupper( c ) && ( previousIsLowerCase || previousIsDigit ) ) || ( isdigit( c ) && previousIsLowerCase ) )
    {
      convertedName.push_back( '_' );
    }
    convertedName.push_back( static_cast<char>( toupper( c ) ) );
    previousIsLowerCase = !!islower( c );
    previousIsDigit     = !!isdigit( c );
  }
  return convertedName;
}

std::vector<std::string> tokenize( std::string const & tokenString, std::string const & separator )
{
  std::vector<std::string> tokens;
  if ( !tokenString.empty() )
  {
    size_t start = 0, end;
    do
    {
      end = tokenString.find( separator, start );
      if ( start != end )
      {
        tokens.push_back( trim( tokenString.substr( start, end - start ) ) );
      }
      start = end + separator.length();
    } while ( end != std::string::npos );
  }
  return tokens;
}

std::vector<std::string> tokenizeAny( std::string const & tokenString, std::string const & separators )
{
  size_t                   len = tokenString.length();
  std::vector<std::string> tokens;
  if ( !tokenString.empty() )
  {
    size_t start = 0, end;
    do
    {
      end = tokenString.find_first_of( separators, start );
      if ( ( start != end ) && ( start < len ) )
      {
        tokens.push_back( trim( tokenString.substr( start, end - start ) ) );
      }
      start = end + 1;
    } while ( end != std::string::npos );
  }
  return tokens;
}

std::string trim( std::string const & input )
{
  std::string result = input;
  result.erase( result.begin(), std::find_if( result.begin(), result.end(), []( char c ) { return !std::isspace( c ); } ) );
  result.erase( std::find_if( result.rbegin(), result.rend(), []( char c ) { return !std::isspace( c ); } ).base(), result.end() );
  return result;
}

std::string trimEnd( std::string const & input )
{
  std::string result = input;
  result.erase( std::find_if( result.rbegin(), result.rend(), []( char c ) { return !std::isspace( c ); } ).base(), result.end() );
  return result;
}

std::string trimStars( std::string const & input )
{
  std::string result = input;
  size_t      pos    = result.find( '*' );
  while ( pos != std::string::npos )
  {
    if ( ( 0 < pos ) && ( result[pos - 1] != ' ' ) && ( result[pos - 1] != '*' ) )
    {
      result.insert( pos, 1, ' ' );
      ++pos;
    }
    else if ( ( pos < result.length() - 1 ) && ( result[pos + 1] != ' ' ) && ( result[pos + 1] != '*' ) )
    {
      result.insert( pos + 1, 1, ' ' );
    }
    pos = result.find( '*', pos + 1 );
  }
  return result;
}

void writeToFile( std::string const & str, std::string const & fileName )
{
  std::ofstream ofs( fileName );
  assert( !ofs.fail() );
  ofs << str;
  ofs.close();

#if defined( CLANG_FORMAT_EXECUTABLE )
  std::cout << "VulkanHppGenerator: Formatting " << fileName << " ..." << std::endl;
  std::string commandString = "\"" CLANG_FORMAT_EXECUTABLE "\" -i --style=file " + fileName;
  int         ret           = std::system( commandString.c_str() );
  if ( ret != 0 )
  {
    std::cout << "VulkanHppGenerator: failed to format file " << fileName << " with error <" << ret << ">\n";
  }
#endif
}

std::string toString( tinyxml2::XMLError error )
{
  switch ( error )
  {
    case tinyxml2::XML_SUCCESS: return "XML_SUCCESS";
    case tinyxml2::XML_NO_ATTRIBUTE: return "XML_NO_ATTRIBUTE";
    case tinyxml2::XML_WRONG_ATTRIBUTE_TYPE: return "XML_WRONG_ATTRIBUTE_TYPE";
    case tinyxml2::XML_ERROR_FILE_NOT_FOUND: return "XML_ERROR_FILE_NOT_FOUND";
    case tinyxml2::XML_ERROR_FILE_COULD_NOT_BE_OPENED: return "XML_ERROR_FILE_COULD_NOT_BE_OPENED";
    case tinyxml2::XML_ERROR_FILE_READ_ERROR: return "XML_ERROR_FILE_READ_ERROR";
    case tinyxml2::XML_ERROR_PARSING_ELEMENT: return "XML_ERROR_PARSING_ELEMENT";
    case tinyxml2::XML_ERROR_PARSING_ATTRIBUTE: return "XML_ERROR_PARSING_ATTRIBUTE";
    case tinyxml2::XML_ERROR_PARSING_TEXT: return "XML_ERROR_PARSING_TEXT";
    case tinyxml2::XML_ERROR_PARSING_CDATA: return "XML_ERROR_PARSING_CDATA";
    case tinyxml2::XML_ERROR_PARSING_COMMENT: return "XML_ERROR_PARSING_COMMENT";
    case tinyxml2::XML_ERROR_PARSING_DECLARATION: return "XML_ERROR_PARSING_DECLARATION";
    case tinyxml2::XML_ERROR_PARSING_UNKNOWN: return "XML_ERROR_PARSING_UNKNOWN";
    case tinyxml2::XML_ERROR_EMPTY_DOCUMENT: return "XML_ERROR_EMPTY_DOCUMENT";
    case tinyxml2::XML_ERROR_MISMATCHED_ELEMENT: return "XML_ERROR_MISMATCHED_ELEMENT";
    case tinyxml2::XML_ERROR_PARSING: return "XML_ERROR_PARSING";
    case tinyxml2::XML_CAN_NOT_CONVERT_TEXT: return "XML_CAN_NOT_CONVERT_TEXT";
    case tinyxml2::XML_NO_TEXT_NODE: return "XML_NO_TEXT_NODE";
    default: return "unknown error code <" + std::to_string( error ) + ">";
  }
}

int main( int argc, char ** argv )
{
  if ( ( argc % 2 ) == 0 )
  {
    std::cout << "VulkanHppGenerator usage: VulkanHppGenerator [-f filename][-api [vulkan|vulkansc]]" << std::endl;
    std::cout << "\tdefault for filename is <" << VK_SPEC << ">" << std::endl;
    std::cout << "\tdefault for api <vulkan>" << std::endl;
    std::cout << "\tsupported values for api are <vulkan> and <vulkansc>" << std::endl;
    return -1;
  }

  std::string api      = "vulkan";
  std::string filename = VK_SPEC;
  for ( int i = 1; i < argc; i += 2 )
  {
    if ( strcmp( argv[i], "-api" ) == 0 )
    {
      api = argv[i + 1];
    }
    else if ( strcmp( argv[i], "-f" ) == 0 )
    {
      filename = argv[i + 1];
    }
    else
    {
      std::cout << "unsupported argument <" << argv[i] << ">" << std::endl;
      return -1;
    }
  }

  if ( ( api != "vulkan" ) && ( api != "vulkansc" ) )
  {
    std::cout << "unsupported api <" << api << ">" << std::endl;
    return -1;
  }

#if defined( CLANG_FORMAT_EXECUTABLE )
  std::cout << "VulkanHppGenerator: Found ";
  std::string commandString = "\"" CLANG_FORMAT_EXECUTABLE "\" --version ";
  int         ret           = std::system( commandString.c_str() );
  if ( ret != 0 )
  {
    std::cout << "VulkanHppGenerator: failed to determine clang_format version with error <" << ret << ">\n";
  }
#endif

  tinyxml2::XMLDocument doc;
  std::cout << "VulkanHppGenerator: Loading " << filename << std::endl;
  tinyxml2::XMLError error = doc.LoadFile( filename.c_str() );
  if ( error != tinyxml2::XML_SUCCESS )
  {
    std::cout << "VulkanHppGenerator: failed to load file " << filename << " with error <" << toString( error ) << ">" << std::endl;
    return -1;
  }

  try
  {
    std::cout << "VulkanHppGenerator: Parsing " << filename << std::endl;
    VulkanHppGenerator generator( doc, api );

    generator.generateHppFile();
    generator.generateEnumsHppFile();
    generator.generateExtensionInspectionFile();
    generator.generateFormatTraitsHppFile();
    generator.prepareVulkanFuncs();
    generator.generateFuncsHppFile();
    generator.generateHandlesHppFile();
    generator.generateHashHppFile();
    generator.prepareRAIIHandles();
    generator.generateRAIIHppFile();
    generator.generateStaticAssertionsHppFile();
    generator.generateStructsHppFile();
    generator.generateToStringHppFile();

#if !defined( CLANG_FORMAT_EXECUTABLE )
    std::cout << "VulkanHppGenerator: could not find clang-format. The generated files will not be formatted accordingly.\n";
#endif
  }
  catch ( std::exception const & e )
  {
    std::cout << "caught exception: " << e.what() << std::endl;
    return -1;
  }
  catch ( ... )
  {
    std::cout << "caught unknown exception" << std::endl;
    return -1;
  }
}
