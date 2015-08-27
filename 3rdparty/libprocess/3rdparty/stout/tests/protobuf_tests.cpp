/**
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License
*/

#include <string>

#include <gtest/gtest.h>

#include <gmock/gmock.h>

#include <stout/gtest.hpp>
#include <stout/json.hpp>
#include <stout/protobuf.hpp>
#include <stout/stringify.hpp>
#include <stout/strings.hpp>
#include <stout/uuid.hpp>

#include "protobuf_tests.pb.h"

using std::string;

namespace tests {

// A trivial equality operator to enable gtest macros.
inline bool operator==(const SimpleMessage& left, const SimpleMessage& right)
{
  if (left.id() != right.id() ||
      left.numbers().size() != right.numbers().size()) {
    return false;
  }

  for (int idx = 0; idx < left.numbers().size(); ++idx) {
    if (left.numbers().Get(idx) != right.numbers().Get(idx)) {
      return false;
    }
  }

  return true;
}

} // namespace tests {


TEST(ProtobufTest, JSON)
{
  tests::Message message;
  message.set_b(true);
  message.set_str("string");
  message.set_bytes("bytes");
  message.set_int32(-1);
  message.set_int64(-1);
  message.set_uint32(1);
  message.set_uint64(1);
  message.set_sint32(-1);
  message.set_sint64(-1);
  message.set_f(1.0);
  message.set_d(1.0);
  message.set_e(tests::ONE);
  message.mutable_nested()->set_str("nested");
  message.add_repeated_bool(true);
  message.add_repeated_string("repeated_string");
  message.add_repeated_bytes("repeated_bytes");
  message.add_repeated_int32(-2);
  message.add_repeated_int64(-2);
  message.add_repeated_uint32(2);
  message.add_repeated_uint64(2);
  message.add_repeated_sint32(-2);
  message.add_repeated_sint64(-2);
  message.add_repeated_float(1.0);
  message.add_repeated_double(1.0);
  message.add_repeated_double(2.0);
  message.add_repeated_enum(tests::TWO);
  message.add_repeated_nested()->set_str("repeated_nested");

  // TODO(bmahler): To dynamically generate a protobuf message,
  // see the commented-out code below.
//  DescriptorProto proto;
//
//  proto.set_name("Message");
//
//  FieldDescriptorProto* field = proto.add_field();
//  field->set_name("str");
//  field->set_type(FieldDescriptorProto::TYPE_STRING);
//
//  const Descriptor* descriptor = proto.descriptor();
//
//  DynamicMessageFactory factory;
//  Message* message = factory.GetPrototype(descriptor);
//
//  Reflection* message.getReflection();

  // The keys are in alphabetical order.
  string expected = strings::remove(
      "{"
      "  \"b\": true,"
      "  \"bytes\": \"Ynl0ZXM=\","
      "  \"d\": 1,"
      "  \"e\": \"ONE\","
      "  \"f\": 1,"
      "  \"int32\": -1,"
      "  \"int64\": -1,"
      "  \"nested\": { \"str\": \"nested\"},"
      "  \"optional_default\": 42,"
      "  \"repeated_bool\": [true],"
      "  \"repeated_bytes\": [\"cmVwZWF0ZWRfYnl0ZXM=\"],"
      "  \"repeated_double\": [1, 2],"
      "  \"repeated_enum\": [\"TWO\"],"
      "  \"repeated_float\": [1],"
      "  \"repeated_int32\": [-2],"
      "  \"repeated_int64\": [-2],"
      "  \"repeated_nested\": [ { \"str\": \"repeated_nested\" } ],"
      "  \"repeated_sint32\": [-2],"
      "  \"repeated_sint64\": [-2],"
      "  \"repeated_string\": [\"repeated_string\"],"
      "  \"repeated_uint32\": [2],"
      "  \"repeated_uint64\": [2],"
      "  \"sint32\": -1,"
      "  \"sint64\": -1,"
      "  \"str\": \"string\","
      "  \"uint32\": 1,"
      "  \"uint64\": 1"
      "}",
      " ");

  JSON::Object object = JSON::Protobuf(message);

  EXPECT_EQ(expected, stringify(object));

  // Test parsing too.
  Try<tests::Message> parse = protobuf::parse<tests::Message>(object);
  ASSERT_SOME(parse);

  EXPECT_EQ(object, JSON::Protobuf(parse.get()));

  // Modify the message to test (de-)serialization of random bytes generated
  // by UUID.
  message.set_bytes(UUID::random().toBytes());

  object = JSON::Protobuf(message);

  // Test parsing too.
  parse = protobuf::parse<tests::Message>(object);
  ASSERT_SOME(parse);

  EXPECT_EQ(object, JSON::Protobuf(parse.get()));

  // Now convert JSON to string and parse it back as JSON.
  ASSERT_SOME_EQ(object, JSON::parse(stringify(object)));
}


TEST(ProtobufTest, ParseJSONArray)
{
  tests::SimpleMessage message1;
  message1.set_id("message1");
  message1.add_numbers(1);
  message1.add_numbers(2);

  // Convert protobuf message to a JSON object.
  JSON::Object object1 = JSON::Protobuf(message1);

  // Populate JSON array with JSON objects, conversion JSON::Object ->
  // JSON::Value is implicit.
  JSON::Array array;
  array.values.push_back(object1);
  array.values.push_back(object1);

  // Parse JSON array into a collection of protobuf messages.
  auto parse = protobuf::parse<tests::SimpleMessage>(array);
  ASSERT_SOME(parse);
  auto repeated = parse.get();

  // We expect both custom protobuf message equality operator and backwards
  // JSON conversion to succeed.
  EXPECT_EQ(message1, repeated.Get(0));
  EXPECT_EQ(message1, repeated.Get(1));

  EXPECT_EQ(object1, JSON::Protobuf(repeated.Get(0)));
  EXPECT_EQ(object1, JSON::Protobuf(repeated.Get(1)));
}
