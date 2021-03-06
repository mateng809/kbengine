/*
This source file is part of KBEngine
For the latest info, see http://www.kbengine.org/

Copyright (c) 2008-2017 KBEngine.

KBEngine is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

KBEngine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.
 
You should have received a copy of the GNU Lesser General Public License
along with KBEngine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "client_sdk.h"
#include "client_sdk_unity.h"	
#include "client_sdk_ue4.h"
#include "entitydef/entitydef.h"
#include "entitydef/scriptdef_module.h"
#include "entitydef/property.h"
#include "entitydef/method.h"
#include "entitydef/datatypes.h"
#include "entitydef/datatype.h"
#include "resmgr/resmgr.h"

#ifdef _WIN32  
#include <direct.h>  
#include <io.h>  
#elif _LINUX  
#include <stdarg.h>  
#include <sys/stat.h>  
#endif  

#if KBE_PLATFORM == PLATFORM_WIN32
#define KBE_ACCESS _access  
#define KBE_MKDIR(a) _mkdir((a))  
#else
#define KBE_ACCESS access  
#define KBE_MKDIR(a) mkdir((a),0755)  
#endif  

namespace KBEngine {	

int CreatDir(const char *pDir)
{
	int i = 0;
	int iRet;
	int iLen;
	char* pszDir;

	if (NULL == pDir)
	{
		return 0;
	}

	pszDir = strdup(pDir);
	iLen = strlen(pszDir);

	// 创建中间目录  
	for (i = 0; i < iLen; i++)
	{
		if (pszDir[i] == '\\' || pszDir[i] == '/')
		{
			pszDir[i] = '\0';

			//如果不存在,创建  
			iRet = KBE_ACCESS(pszDir, 0);
			if (iRet != 0)
			{
				iRet = KBE_MKDIR(pszDir);
				if (iRet != 0)
				{
					free(pszDir);
					return -1;
				}
			}

			//支持linux,将所有\换成/  
			pszDir[i] = '/';
		}
	}

	if (iLen > 0 && KBE_ACCESS(pszDir, 0) != 0)
	{
		iRet = KBE_MKDIR(pszDir);
	}
	
	free(pszDir);
	return iRet;
}

//-------------------------------------------------------------------------------------
ClientSDK::ClientSDK():
	basepath_(),
	currpath_(),
	sourcefileBody_(),
	sourcefileName_()
{

}

//-------------------------------------------------------------------------------------
ClientSDK::~ClientSDK()
{

}

//-------------------------------------------------------------------------------------
ClientSDK* ClientSDK::createClientSDK(const std::string& type)
{
	std::string lowerType = type;
	std::transform(lowerType.begin(), lowerType.end(), lowerType.begin(), tolower);

	if (lowerType == "unity")
	{
		return new ClientSDKUnity();
	}
	else if(lowerType == "ue4")
	{
		return new ClientSDKUE4();
	}

	return NULL;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::good() const
{
	return true;
}

//-------------------------------------------------------------------------------------
void ClientSDK::onCreateEntityModuleFileName(const std::string& moduleName)
{
	sourcefileName_ = moduleName + ".unknown";
}

//-------------------------------------------------------------------------------------
bool ClientSDK::saveFile()
{
	if (CreatDir(currpath_.c_str()) == -1)
	{
		ERROR_MSG(fmt::format("creating directory error! path={}\n", currpath_));
		return false;
	}

	std::string path = currpath_ + sourcefileName_;
	
	DEBUG_MSG(fmt::format("ClientSDK::saveFile(): {}\n",
		path));

	FILE *fp = fopen(path.c_str(), "w");

	if (NULL == fp)
	{
		ERROR_MSG(fmt::format("ClientSDK::saveFile(): fopen error! {}\n",
			path));

		return false;
	}

	int written = fwrite(sourcefileBody_.c_str(), 1, sourcefileBody_.size(), fp);
	if (written != (int)sourcefileBody_.size())
	{
		ERROR_MSG(fmt::format("ClientSDK::saveFile(): fwrite error! {}\n",
			path));

		return false;
	}

	if(fclose(fp))
	{
		ERROR_MSG(fmt::format("ClientSDK::saveFile(): fclose error! {}\n",
			path));

		return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::create(const std::string& path)
{
	basepath_ = path;

	if (basepath_[basepath_.size() - 1] != '\\' && basepath_[basepath_.size() - 1] != '/')
#ifdef _WIN32  
		basepath_ += "/";
#else
		basepath_ += "\\";
#endif

	currpath_ = basepath_;

	std::string findpath = "client/sdk_templates/" + name();

	std::string getpath = Resmgr::getSingleton().matchPath(findpath);

	if (getpath.size() == 0 || findpath == getpath)
	{
		ERROR_MSG(fmt::format("ClientSDK::create(): not found path({})\n",
			findpath));

		return false;
	}

	if (!copyPluginsSourceToPath(getpath))
		return false;

	if (!writeServerErrorDescrs())
		return false;

	if (!writeTypes())
		return false;

	const EntityDef::SCRIPT_MODULES& scriptModules = EntityDef::getScriptModules();
	EntityDef::SCRIPT_MODULES::const_iterator moduleIter = scriptModules.begin();
	for (; moduleIter != scriptModules.end(); ++moduleIter)
	{
		ScriptDefModule* pScriptDefModule = (*moduleIter).get();

		if (!writeEntityModule(pScriptDefModule))
			return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------
void ClientSDK::onCreateTypeFileName()
{
	sourcefileName_ = "kbe_types.unknown";
}

//-------------------------------------------------------------------------------------
void ClientSDK::onCreateServerErrorDescrsModuleFileName()
{
	sourcefileName_ = "servererrdescrs.unknown";
}

//-------------------------------------------------------------------------------------
bool ClientSDK::copyPluginsSourceToPath(const std::string& path)
{
	wchar_t* wpath = strutil::char2wchar(path.c_str());
	std::wstring sourcePath = wpath;
	free(wpath);

	wpath = strutil::char2wchar(basepath_.c_str());
	std::wstring destPath = wpath;
	free(wpath);
	

	std::vector<std::wstring> results;
	if (!Resmgr::getSingleton().listPathRes(sourcePath, L"*", results))
		return false;

	wchar_t* wfindpath = strutil::char2wchar(std::string("client/sdk_templates/" + name()).c_str());
	std::wstring findpath = wfindpath;
	free(wfindpath);

	std::vector<std::wstring>::iterator iter = results.begin();
	for (; iter != results.end(); ++iter)
	{
		std::wstring::size_type fpos = (*iter).find(findpath);

		char* ccattr = strutil::wchar2char((*iter).c_str());
		std::string currpath = ccattr;
		free(ccattr);

		if (fpos == std::wstring::npos)
		{
			ERROR_MSG(fmt::format("ClientSDK::copyPluginsSourceToPath(): split path({}) error!\n",
				currpath));

			return false;
		}

		std::wstring targetFile = (*iter);
		targetFile.erase(0, fpos + findpath.size() + 1);
		targetFile = (destPath + targetFile);

		std::wstring basepath = targetFile;
		fpos = targetFile.rfind(L"/");

		ccattr = strutil::wchar2char(targetFile.c_str());
		std::string currTargetFile = ccattr;
		free(ccattr);

		if (fpos == std::wstring::npos)
		{
			ERROR_MSG(fmt::format("ClientSDK::copyPluginsSourceToPath(): split basepath({}) error!\n",
				currTargetFile));

			return false;
		}

		basepath.erase(fpos, basepath.size() - fpos);
		
		ccattr = strutil::wchar2char(basepath.c_str());
		std::string currbasepath = ccattr;
		free(ccattr);

		if (CreatDir(currbasepath.c_str()) == -1)
		{
			ERROR_MSG(fmt::format("ClientSDK::copyPluginsSourceToPath(): creating directory error! path={}\n", currbasepath));
			return false;
		}

		std::ifstream input(currpath.c_str(), std::ios::binary);
		std::ofstream output(currTargetFile.c_str(), std::ios::binary);
		output << input.rdbuf();
		output.close();
		input.close();
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeServerErrorDescrs()
{
	std::map<uint16, std::pair< std::string, std::string> > errsDescrs;

	{
		TiXmlNode *rootNode = NULL;
		SmartPointer<XML> xml(new XML(Resmgr::getSingleton().matchRes("server/server_errors_defaults.xml").c_str()));

		if (!xml->isGood())
		{
			ERROR_MSG(fmt::format("ClientSDK::writeServerErrors: load {} is failed!\n",
				"server/server_errors_defaults.xml"));

			return false;
		}

		rootNode = xml->getRootNode();
		if (rootNode)
		{
			XML_FOR_BEGIN(rootNode)
			{
				TiXmlNode* node = xml->enterNode(rootNode->FirstChild(), "id");
				TiXmlNode* node1 = xml->enterNode(rootNode->FirstChild(), "descr");
				errsDescrs[xml->getValInt(node)] = std::make_pair< std::string, std::string>(xml->getKey(rootNode), xml->getVal(node1));
			}
			XML_FOR_END(rootNode);
		}
	}

	{
		TiXmlNode *rootNode = NULL;
		SmartPointer<XML> xml(new XML(Resmgr::getSingleton().matchRes("server/server_errors.xml").c_str()));

		if (xml->isGood())
		{

			rootNode = xml->getRootNode();
			if (rootNode)
			{
				XML_FOR_BEGIN(rootNode)
				{
					TiXmlNode* node = xml->enterNode(rootNode->FirstChild(), "id");
					TiXmlNode* node1 = xml->enterNode(rootNode->FirstChild(), "descr");
					errsDescrs[xml->getValInt(node)] = std::make_pair< std::string, std::string>(xml->getKey(rootNode), xml->getVal(node1));
				}
				XML_FOR_END(rootNode);
			}
		}
	}

	sourcefileName_ = sourcefileBody_ = "";
	onCreateServerErrorDescrsModuleFileName();

	DEBUG_MSG(fmt::format("ClientSDK::writeServerErrorDescrs(): {}/{}\n",
		basepath_, sourcefileName_));

	if (!writeServerErrorDescrsModuleBegin())
		return false;

	std::map<uint16, std::pair< std::string, std::string> >::iterator iter = errsDescrs.begin();

	for (; iter != errsDescrs.end(); ++iter)
	{
		if (!writeServerErrorDescrsModuleErrDescr(iter->first, iter->second.first, iter->second.second))
			return false;
	}

	if (!writeServerErrorDescrsModuleEnd())
		return false;

	return saveFile();
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeServerErrorDescrsModuleBegin()
{
	ERROR_MSG(fmt::format("ClientSDK::writeServerErrorDescrsModuleBegin: Not Implemented!\n"));
	return false;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeServerErrorDescrsModuleErrDescr(int errorID, const std::string& errname, const std::string& errdescr)
{
	ERROR_MSG(fmt::format("ClientSDK::writeServerErrorDescrsModuleErrDescr: Not Implemented!\n"));
	return false;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeServerErrorDescrsModuleEnd()
{
	ERROR_MSG(fmt::format("ClientSDK::writeServerErrorDescrsModuleEnd: Not Implemented!\n"));
	return false;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeTypes()
{
	sourcefileName_ = sourcefileBody_ = "";
	onCreateTypeFileName();

	if (!writeTypesBegin())
		return false;

	const DataTypes::DATATYPE_MAP& dataTypes = DataTypes::dataTypes();
	

	const DataTypes::DATATYPE_ORDERS& dataTypesOrders = DataTypes::dataTypesOrders();
	DataTypes::DATATYPE_ORDERS::const_iterator oiter = dataTypesOrders.begin();

	for (; oiter != dataTypesOrders.end(); ++oiter)
	{
		DataTypes::DATATYPE_MAP::const_iterator iter = dataTypes.find((*oiter));

		std::string typeName = iter->first;

		if (typeName[0] == '_')
			continue;

		DataType* pDataType = iter->second.get();

		if (pDataType->type() == DATA_TYPE_FIXEDDICT)
		{
			FixedDictType* pFixedDictType = static_cast<FixedDictType*>(pDataType);

			if (!writeTypeBegin(typeName, pFixedDictType))
				return false;

			FixedDictType::FIXEDDICT_KEYTYPE_MAP& keyTypes = pFixedDictType->getKeyTypes();
			FixedDictType::FIXEDDICT_KEYTYPE_MAP::iterator itemIter = keyTypes.begin();
			for(; itemIter != keyTypes.end(); ++itemIter)
			{
				std::string type = itemIter->second->dataType->getName();
				std::string itemTypeName = itemIter->first;
				std::string itemTypeAliasName = itemIter->second->dataType->aliasName();

				if (type == "INT8")
				{
					if (!writeTypeItemType_INT8(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "INT16")
				{
					if (!writeTypeItemType_INT16(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "INT32")
				{
					if (!writeTypeItemType_INT32(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "INT64")
				{
					if (!writeTypeItemType_INT64(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "UINT8")
				{
					if (!writeTypeItemType_UINT8(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "UINT16")
				{
					if (!writeTypeItemType_UINT16(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "UINT32")
				{
					if (!writeTypeItemType_UINT32(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "UINT64")
				{
					if (!writeTypeItemType_UINT64(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "FLOAT")
				{
					if (!writeTypeItemType_FLOAT(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "DOUBLE")
				{
					if (!writeTypeItemType_DOUBLE(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "STRING")
				{
					if (!writeTypeItemType_STRING(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "UNICODE")
				{
					if (!writeTypeItemType_UNICODE(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "PYTHON")
				{
					if (!writeTypeItemType_PYTHON(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "PY_DICT")
				{
					if (!writeTypeItemType_PY_DICT(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "PY_TUPLE")
				{
					if (!writeTypeItemType_PY_TUPLE(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "PY_LIST")
				{
					if (!writeTypeItemType_PY_LIST(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "BLOB")
				{
					if (!writeTypeItemType_BLOB(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "ARRAY")
				{
					if (!writeTypeItemType_ARRAY(itemTypeName, itemTypeAliasName, itemIter->second->dataType))
						return false;
				}
				else if (type == "FIXED_DICT")
				{
					if (!writeTypeItemType_FIXED_DICT(itemTypeName, itemTypeAliasName, itemIter->second->dataType))
						return false;
				}
#ifdef CLIENT_NO_FLOAT
				else if (type == "VECTOR2")
				{
					if (!writeTypeItemType_VECTOR2(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "VECTOR3")
				{
					if (!writeTypeItemType_VECTOR3(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "VECTOR4")
				{
					if (!writeTypeItemType_VECTOR4(itemTypeName, itemTypeAliasName))
						return false;
				}
#else
				else if (type == "VECTOR2")
				{
					if (!writeTypeItemType_VECTOR2(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "VECTOR3")
				{
					if (!writeTypeItemType_VECTOR3(itemTypeName, itemTypeAliasName))
						return false;
				}
				else if (type == "VECTOR4")
				{
					if (!writeTypeItemType_VECTOR4(itemTypeName, itemTypeAliasName))
						return false;
				}
#endif
				else if (type == "MAILBOX")
				{
					if (!writeTypeItemType_MAILBOX(itemTypeName, itemTypeAliasName))
						return false;
				}
			}

			if (!writeTypeEnd(typeName, pFixedDictType))
				return false;
		}
		else if (pDataType->type() == DATA_TYPE_FIXEDARRAY)
		{
			FixedArrayType* pFixedArrayType = static_cast<FixedArrayType*>(pDataType);

			if (!writeTypeBegin(typeName, pFixedArrayType, fmt::format("{}<#REPLACE#>", typeToType("ARRAY"))))
				return false;

			std::string type = pFixedArrayType->getDataType()->getName();
			std::string itemTypeAliasName = pFixedArrayType->getDataType()->aliasName();

			if (type != "ARRAY" && type != "FIXED_DICT")
			{
				std::string newType = typeToType(type);
				strutil::kbe_replace(sourcefileBody_, "#REPLACE#", newType);

				std::string::size_type fpos = sourcefileBody_.find("#REPLACE#");
				KBE_ASSERT(fpos == std::string::npos);
			}

			if (type == "ARRAY")
			{
				if (!writeTypeItemType_ARRAY(typeName, itemTypeAliasName, pFixedArrayType->getDataType()))
					return false;
			}
			else if (type == "FIXED_DICT")
			{
				if (!writeTypeItemType_FIXED_DICT(typeName, itemTypeAliasName, pFixedArrayType->getDataType()))
					return false;
			}

			strutil::kbe_replace(sourcefileBody_, "#REPLACE#", "object");

			if (!writeTypeEnd(typeName, pFixedArrayType))
				return false;
		}
		else
		{
		}
	}

	if (!writeTypesEnd())
		return false; 

	return saveFile();
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeTypesBegin()
{
	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeTypesEnd()
{
	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityModule(ScriptDefModule* pEntityScriptDefModule)
{
	DEBUG_MSG(fmt::format("ClientSDK::writeEntityModule(): {}/{}\n",
		currpath_, pEntityScriptDefModule->getName()));

	sourcefileName_ = sourcefileBody_ = "";
	onCreateEntityModuleFileName(pEntityScriptDefModule->getName());

	if (!writeEntityModuleBegin(pEntityScriptDefModule))
		return false;

	if (!writeEntityPropertys(pEntityScriptDefModule, pEntityScriptDefModule))
		return false;

	if (!writeEntityMethods(pEntityScriptDefModule, pEntityScriptDefModule))
		return false;

	if (!writeEntityModuleEnd(pEntityScriptDefModule))
		return false;

	return saveFile();
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityModuleBegin(ScriptDefModule* pEntityScriptDefModule)
{
	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityModuleEnd(ScriptDefModule* pEntityScriptDefModule)
{
	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityPropertys(ScriptDefModule* pEntityScriptDefModule,
	ScriptDefModule* pCurrScriptDefModule)
{
	ScriptDefModule::PROPERTYDESCRIPTION_MAP& clientPropertys = pCurrScriptDefModule->getClientPropertyDescriptions();
	ScriptDefModule::PROPERTYDESCRIPTION_MAP::const_iterator propIter = clientPropertys.begin();
	for (; propIter != clientPropertys.end(); ++propIter)
	{
		PropertyDescription* pPropertyDescription = propIter->second;
		if (!writeEntityProperty(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription))
			return false;
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityProperty(ScriptDefModule* pEntityScriptDefModule,
	ScriptDefModule* pCurrScriptDefModule, PropertyDescription* pPropertyDescription)
{
	std::string type = pPropertyDescription->getDataType()->getName();

	if (type == "INT8")
	{
		return writeEntityProperty_INT8(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "INT16")
	{
		return writeEntityProperty_INT16(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "INT32")
	{
		return writeEntityProperty_INT32(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "INT64")
	{
		return writeEntityProperty_INT64(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "UINT8")
	{
		return writeEntityProperty_UINT8(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "UINT16")
	{
		return writeEntityProperty_UINT16(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "UINT32")
	{
		return writeEntityProperty_UINT32(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "UINT64")
	{
		return writeEntityProperty_UINT64(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "FLOAT")
	{
		return writeEntityProperty_FLOAT(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "DOUBLE")
	{
		return writeEntityProperty_DOUBLE(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "STRING")
	{
		return writeEntityProperty_STRING(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "UNICODE")
	{
		return writeEntityProperty_UNICODE(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "PYTHON")
	{
		return writeEntityProperty_PYTHON(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "PY_DICT")
	{
		return writeEntityProperty_PY_DICT(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "PY_TUPLE")
	{
		return writeEntityProperty_PY_TUPLE(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "PY_LIST")
	{
		return writeEntityProperty_PY_LIST(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "BLOB")
	{
		return writeEntityProperty_BLOB(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "ARRAY")
	{
		return writeEntityProperty_ARRAY(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "FIXED_DICT")
	{
		return writeEntityProperty_FIXED_DICT(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
#ifdef CLIENT_NO_FLOAT
	else if (type == "VECTOR2")
	{
		return writeEntityProperty_VECTOR2(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "VECTOR3")
	{
		return writeEntityProperty_VECTOR3(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "VECTOR4")
	{
		return writeEntityProperty_VECTOR4(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
#else
	else if (type == "VECTOR2")
	{
		return writeEntityProperty_VECTOR2(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "VECTOR3")
	{
		return writeEntityProperty_VECTOR3(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
	else if (type == "VECTOR4")
	{
		return writeEntityProperty_VECTOR4(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}
#endif
	else if (type == "MAILBOX")
	{
		return writeEntityProperty_MAILBOX(pEntityScriptDefModule, pCurrScriptDefModule, pPropertyDescription);
	}

	assert(false);
	return false;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityMethods(ScriptDefModule* pEntityScriptDefModule,
	ScriptDefModule* pCurrScriptDefModule)
{
	sourcefileBody_ += "\n\n";

	ScriptDefModule::METHODDESCRIPTION_MAP& clientMethods = pCurrScriptDefModule->getClientMethodDescriptions();
	ScriptDefModule::METHODDESCRIPTION_MAP::iterator methodIter = clientMethods.begin();
	for (; methodIter != clientMethods.end(); ++methodIter)
	{
		MethodDescription* pMethodDescription = methodIter->second;
		if (!writeEntityMethod(pEntityScriptDefModule, pCurrScriptDefModule, pMethodDescription, "#REPLACE#"))
			return false;

		std::string::size_type fpos = sourcefileBody_.find("#REPLACE#");
		KBE_ASSERT(fpos != std::string::npos);

		std::string argsBody = "";

		std::vector<DataType*>& argTypes = pMethodDescription->getArgTypes();
		std::vector<DataType*>::iterator iter = argTypes.begin();

		int i = 1;

		for (; iter != argTypes.end(); ++iter)
		{
			DataType* pDataType = (*iter);

			if (pDataType->type() == DATA_TYPE_FIXEDARRAY)
			{
				FixedArrayType* pFixedArrayType = static_cast<FixedArrayType*>(pDataType);
				
				std::string argsTypeBody;
				if (!writeEntityMethodArgs_ARRAY(pFixedArrayType, argsTypeBody, pFixedArrayType->aliasName()))
				{
					return false;
				}

				argsBody += fmt::format("{} param{}, ", argsTypeBody, i++);
			}
			else if (pDataType->type() == DATA_TYPE_FIXEDDICT)
			{
				FixedDictType* pFixedDictType = static_cast<FixedDictType*>(pDataType);

				std::string argsTypeBody = typeToType(pFixedDictType->aliasName());
				if (!writeEntityMethodArgs_Const_Ref(pDataType, argsTypeBody))
				{
					return false;
				}

				argsBody += fmt::format("{} param{}, ", argsTypeBody, i++);
			}
			else if (pDataType->type() != DATA_TYPE_DIGIT)
			{
				std::string argsTypeBody = typeToType(pDataType->getName());
				if (!writeEntityMethodArgs_Const_Ref(pDataType, argsTypeBody))
				{
					return false;
				}

				argsBody += fmt::format("{} param{}, ", argsTypeBody, i++);
			}
			else
			{
				argsBody += fmt::format("{} param{}, ", typeToType(pDataType->getName()), i++);
			}
		}

		if (argsBody.size() > 0)
		{
			argsBody.erase(argsBody.size() - 2, 1);
		}

		strutil::kbe_replace(sourcefileBody_, "#REPLACE#", argsBody);
	}

	return true;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityMethodArgs_ARRAY(FixedArrayType* pFixedArrayType, std::string& stackArgsTypeBody, const std::string& childItemName)
{
	return false;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityMethodArgs_Const_Ref(DataType* pDataType, std::string& stackArgsTypeBody)
{
	return false;
}

//-------------------------------------------------------------------------------------
bool ClientSDK::writeEntityMethod(ScriptDefModule* pEntityScriptDefModule,
	ScriptDefModule* pCurrScriptDefModule, MethodDescription* pMethodDescription, const char* fillString)
{
	return false;
}

//-------------------------------------------------------------------------------------
}
