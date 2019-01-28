/*
 * FogLAMP storage service client
 *
 * Copyright (c) 2018 Dianomic Systems
 *
 * Released under the Apache 2.0 Licence
 *
 * Author: Mark Riddoch, Massimiliano Pinto
 */
#include <reading_set.h>
#include <string>
#include <rapidjson/document.h>
#include <sstream>
#include <iostream>
#include <time.h>
#include <stdlib.h>
#include <logger.h>

#include <boost/algorithm/string/replace.hpp>

#define ASSET_NAME_INVALID_READING "error_invalid_reading"

using namespace std;
using namespace rapidjson;

// List of characters to be escaped in JSON
const vector<string> JSON_characters_to_be_escaped = {
	"\\",
	"\""
};

/**
 * Construct an empty reading set
 */
ReadingSet::ReadingSet() : m_count(0)
{
}

/**
 * Construct a reading set from a vector<Reading *> pointer
 * NOTE: readings are copied into m_readings
 *
 * @param readings	The  vector<Reading *> pointer
 *			of readings to be copied
 *			into m_readings vector
 */
ReadingSet::ReadingSet(vector<Reading *>* readings)
{
	m_count = readings->size();
	for (auto it = readings->begin(); it != readings->end(); ++it)
	{
		m_readings.push_back(*it);
	}
}

/**
 * Construct a reading set from a JSON document returned from
 * the FogLAMP storage service query or notification.
 *
 * @param json	The JSON document (as string) with readings data
 */
ReadingSet::ReadingSet(const std::string& json)
{
	unsigned long rows = 0;
	Document doc;
	Logger::getLogger()->info("ReadingSet c'tor: json=%s", json.c_str());
	doc.Parse(json.c_str());
	if (doc.HasParseError())
	{
		throw new ReadingSetException("Unable to parse results json document");
	}
	// Check we have "count" and "rows"
	bool docHasRows =  doc.HasMember("rows"); // Query
	bool docHasReadings =  doc.HasMember("readings"); // Notification

	// Check we have "rows" or "readings"
	if (!docHasRows && !docHasReadings)
	{
		throw new ReadingSetException("Missing readings or rows array");
	}
	PRINT_FUNC;

	// Check we have "count" and "rows"
	if (doc.HasMember("count") && docHasRows)
	{
		PRINT_FUNC;
		m_count = doc["count"].GetUint();
		// No readings
		if (!m_count)
		{
			m_last_id = 0;
			return;
		}
	}
	else
	{
		PRINT_FUNC;
		// These fields might be updated later
		m_count = 0;
		m_last_id = 0;
	}

	PRINT_FUNC;

	// Get "rows" or "readings" data
	const Value& readings = docHasRows ? doc["rows"] : doc["readings"];
	if (readings.IsArray())
	{
		unsigned long id = 0;
		// Process every rows and create the result set
		for (auto& reading : readings.GetArray())
		{
			if (!reading.IsObject())
			{
				throw new ReadingSetException("Expected reading to be an object");
			}
			PRINT_FUNC;
			JSONReading *value = new JSONReading(reading);
			Logger::getLogger()->info("ReadingSet c'tor: value=%s", value->toJSON().c_str());
			PRINT_FUNC;
			m_readings.push_back(value);

			// Get the Reading Id
			id = value->getId();

			// We don't have count informations with "readings"
			if (docHasReadings)
			{
				rows++;
			}

		}
		// Set the last id
		m_last_id = id;

		// Set count informations with "readings"
		if (docHasReadings)
		{
			m_count = rows;
		}
	}
	else
	{
		throw new ReadingSetException("Expected array of rows in result set");
	}
}

/**
 * Destructor for a result set
 */
ReadingSet::~ReadingSet()
{
	/* Delete the readings */
	for (auto it = m_readings.cbegin(); it != m_readings.cend(); it++)
	{
		delete *it;
	}
}

/**
 * Append the readings in a second reading set to this reading set.
 * The readings are removed from the original reading set
 */
void
ReadingSet::append(ReadingSet *readings)
{
	append(readings->getAllReadings());
	readings->clear();
}

/**
 * Append the readings in a second reading set to this reading set.
 * The readings are removed from the original reading set
 */
void
ReadingSet::append(ReadingSet& readings)
{
	append(readings.getAllReadings());
	readings.clear();
}

/**
 * Append a set of readings to this reading set.
 */
void
ReadingSet::append(const vector<Reading *>& readings)
{
	for (auto it = readings.cbegin(); it != readings.cend(); it++)
	{
		m_readings.push_back(*it);
		m_count++;
	}
}

/**
 * Remove all readings from the reading set and delete the memory
 * After this call the reading set exists but contains no readings.
 */
void
ReadingSet::removeAll()
{
	for (auto it = m_readings.cbegin(); it != m_readings.cend(); it++)
	{
		delete *it;
	}
	m_readings.clear();
}

/**
 * Remove the readings from the vector without deleting them
 */
void
ReadingSet::clear()
{
	m_readings.clear();
}

/**
 * Convert an ASCII timestamp into a timeval structure
 */
static void convert_timestamp(const char *str, struct timeval *tv)
{
	struct tm tm  = {0};

	memset(&tm, 0, sizeof(tm));
	strptime(str, "%Y-%m-%d %H:%M:%S", &tm);

    	// stores in timeval the UTC time
	tv->tv_sec = mktime(&tm) - __timezone;

	// Work out the microseconds from the fractional part of the seconds
	char fractional[10];
	sscanf(str, "%*d-%*d-%*d %*d:%*d:%*d.%[0-9]*", fractional);
	int multiplier = 6 - (int)strlen(fractional);
	if (multiplier < 0)
		multiplier = 0;
	while (multiplier--)
		strcat(fractional, "0");
	tv->tv_usec = atol(fractional);
}

#if 1
DatapointValue* createDictDPV(const Value &obj)
{
	vector<Datapoint*> *dpVec = new vector<Datapoint*>();
	
	for (Value::ConstMemberIterator itr = obj.MemberBegin();
			itr != obj.MemberEnd(); ++itr)
	{
		DatapointValue * dpv = NULL;
		Logger::getLogger()->info("%s: itr->value.GetType()=%d, kObjectType=%d, kStringType=%d, kNumberType=%d, kArrayType=%d", 
														__FUNCTION__, itr->value.GetType(), kObjectType, kStringType, kNumberType, kArrayType);
		if (itr->value.IsString())
			dpv = new DatapointValue(string(itr->value.GetString()));
		else if (itr->value.IsDouble())
			dpv = new DatapointValue(itr->value.GetDouble());
		else if (itr->value.IsNumber())
			dpv = new DatapointValue((const long) (itr->value.GetInt()));
		else if (itr->value.IsObject())
		{
			dpv = NULL; // TODO
			if (itr->value.IsArray())
			{
				Logger::getLogger()->info("createDPV: Got list inside dict");
				//dpv = createListDPV(itr->value);
			}
			else
			{
				Logger::getLogger()->info("createDPV: Got dict inside dict");
			}
		}
		if (dpv)
			dpVec->emplace_back(new Datapoint(string(itr->name.GetString()), *dpv));
	}

	DatapointValue *dpv = NULL;
	if (dpVec->size() > 0)
	{
		dpv = new DatapointValue(dpVec, true);
		Logger::getLogger()->info("%s: dpv=%s", __FUNCTION__, dpv?dpv->toString().c_str():"NULL");
		return dpv;
	}
	else
	{
		Logger::getLogger()->info("%s: dpv=%s", __FUNCTION__, "NULL");
	}
	
	Logger::getLogger()->info("%s: dpv = %s", __FUNCTION__, dpv?dpv->toString().c_str():"NULL");
	return dpv;
}
#endif

/**
 * Construct a reading from a JSON document
 *
 * The data can be in the "value" property as single numeric value
 * or in the JSON "reading" with different values and types
 *
 * @param json	The JSON document that contains the reading
 */
JSONReading::JSONReading(const Value& json)
{
	if (json.HasMember("id"))
	{
		m_id = json["id"].GetUint();
		m_has_id = true;
	}
	else
	{
		m_has_id = false;
	}
	PRINT_FUNC;
	m_asset = json["asset_code"].GetString();
	convert_timestamp(json["user_ts"].GetString(), &m_userTimestamp);
	if (json.HasMember("ts"))
	{
		convert_timestamp(json["ts"].GetString(), &m_timestamp);
	}
	else
	{
		m_timestamp = m_userTimestamp;
	}
	m_uuid = json["read_key"].GetString();
	PRINT_FUNC;

	// We have a single value here which is a number
	if (json.HasMember("value") && json["value"].IsNumber())
	{
		const Value &m = json["value"];
		
		if (m.IsInt() ||
		    m.IsUint() ||
		    m.IsInt64() ||
		    m.IsUint64())
		{
			PRINT_FUNC;
			DatapointValue* value;
			if (m.IsInt() ||
			    m.IsUint() )
			{
				value = new DatapointValue((long) m.GetInt());
			}
			else
			{
				value = new DatapointValue((long) m.GetInt64());
			}
			this->addDatapoint(new Datapoint("value",*value));
			delete value;

		}
		else if (m.IsDouble())
		{
			PRINT_FUNC;
			DatapointValue value(m.GetDouble());
			this->addDatapoint(new Datapoint("value",
							 value));
		}
		else
		{
			PRINT_FUNC;
			string errMsg = "Cannot parse the numeric type";
			errMsg += " of reading element '";
			errMsg.append("value");
			errMsg += "'";

			throw new ReadingSetException(errMsg.c_str());
		}
	}
	else
	{
		PRINT_FUNC;
		if (json["reading"].IsObject())
		{
			// Add 'reading' values
			for (auto &m : json["reading"].GetObject()) {
				switch (m.value.GetType()) {
					// String
					case (kStringType): {
						PRINT_FUNC;
						DatapointValue value(m.value.GetString());
						this->addDatapoint(new Datapoint(m.name.GetString(),
										 value));
						break;
					}

						// Number
					case (kNumberType): {
						if (m.value.IsInt() ||
						    m.value.IsUint() ||
						    m.value.IsInt64() ||
						    m.value.IsUint64()) {
						    PRINT_FUNC;

							DatapointValue *value;
							if (m.value.IsInt() ||
							    m.value.IsUint()) {
								value = new DatapointValue((long) m.value.GetInt());
							} else {
								value = new DatapointValue((long) m.value.GetInt64());
							}
							this->addDatapoint(new Datapoint(m.name.GetString(),
											 *value));
							delete value;
							break;
						} else if (m.value.IsDouble()) {
						PRINT_FUNC;
							DatapointValue value(m.value.GetDouble());
							this->addDatapoint(new Datapoint(m.name.GetString(),
											 value));
							break;
						} else {
						PRINT_FUNC;
							string errMsg = "Cannot parse the numeric type";
							errMsg += " of reading element '";
							errMsg.append(m.name.GetString());
							errMsg += "'";

							throw new ReadingSetException(errMsg.c_str());
							break;
						}
					}
					
					case kArrayType: {
						PRINT_FUNC;
						vector<Datapoint*> *dpVec = new vector<Datapoint*>();
						for (auto& elem : m.value.GetArray())
						{
							if (!elem.IsObject())
							{
								throw new ReadingSetException("Expected reading to be an object");
							}
							PRINT_FUNC;
							Logger::getLogger()->info("JSONReading c'tor: kArrayType: elem.GetType()=%d, kObjectType=%d, kStringType=%d, kNumberType=%d, kArrayType=%d", 
														elem.GetType(), kObjectType, kStringType, kNumberType, kArrayType);
							DatapointValue *dpv = createDictDPV(elem);
							dpVec->emplace_back(new Datapoint(string("unnamed_list_elem#"), *dpv));
							//JSONReading *value = new JSONReading(elem);
						}
						if (dpVec->size() > 0)
						{
							DatapointValue *dpv = new DatapointValue(dpVec, false);
							Logger::getLogger()->info("%s: dpv=%s", __FUNCTION__, dpv?dpv->toString().c_str():"NULL");
							this->addDatapoint(new Datapoint("value", *dpv));
						}
						else
						{
							Logger::getLogger()->info("%s: dpv=%s", __FUNCTION__, "NULL");
							string errMsg = "Cannot parse the array type with name";
							//errMsg.append(m.name);

							throw new ReadingSetException(errMsg.c_str());
						}
						
						//DatapointValue value(m.value.GetString());
						//this->addDatapoint(new Datapoint(m.name.GetString(),
						//				 value));
						break;
					}

					default: {
						PRINT_FUNC;
						string errMsg = "Cannot handle unsupported type '" + m.value.GetType();
						errMsg += "' of reading element '";
						errMsg.append(m.name.GetString());
						errMsg += "'";
						Logger::getLogger()->info("JSONReading c'tor: errMsg=%s", errMsg.c_str());
						throw new ReadingSetException(errMsg.c_str());

						break;
					}
				}
			}
		}
		else
		{
			// The reading should be an object at this stage, it is and invalid one if not
			// the asset name ASSET_NAME_INVALID_READING will be created in the PI-Server containing the
			// invalid asset_name/values.
			PRINT_FUNC;
			if (json["reading"].IsString())
			{
				string tmp_reading1 = json["reading"].GetString();

				// Escape specific character for to be properly manage as JSON
				for (const string &item : JSON_characters_to_be_escaped)
				{

					escapeCharacter(tmp_reading1, item);
				}

				Logger::getLogger()->error(
					"Invalid reading: Asset name |%s| reading value |%s| converted value |%s|",
					m_asset.c_str(),
					json["reading"].GetString(),
					tmp_reading1.c_str());

				DatapointValue value(tmp_reading1);
				this->addDatapoint(new Datapoint(m_asset, value));

			} else if (json["reading"].IsInt() ||
				   json["reading"].IsUint() ||
				   json["reading"].IsInt64() ||
				   json["reading"].IsUint64()) {
				   PRINT_FUNC;

				DatapointValue *value;

				if (json["reading"].IsInt() ||
				    json["reading"].IsUint()) {
					value = new DatapointValue((long) json["reading"].GetInt());
				} else {
					value = new DatapointValue((long) json["reading"].GetInt64());
				}
				this->addDatapoint(new Datapoint(m_asset, *value));
				delete value;

			} else if (json["reading"].IsDouble())
			{
				PRINT_FUNC;
				DatapointValue value(json["reading"].GetDouble());
				this->addDatapoint(new Datapoint(m_asset, value));

			}
			else
				PRINT_FUNC;

			m_asset = string(ASSET_NAME_INVALID_READING) + string("_") + m_asset.c_str();
			PRINT_FUNC;
		}
		PRINT_FUNC;
	}
	PRINT_FUNC;
}

/**
 * Escapes a character in a string to be properly handled as JSON
 *
 */
void JSONReading::escapeCharacter(string& stringToEvaluate, string pattern)
{
	string escaped = "\\" + pattern;

	boost::replace_all(stringToEvaluate, pattern, escaped);
}