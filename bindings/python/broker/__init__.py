
try:
    from . import _broker
except ImportError:
    import _broker

from . import utils

import datetime
import ipaddress

Version = _broker.Version
Version.string = lambda: '%u.%u.%u' % (Version.MAJOR, Version.MINOR, Version.PATCH)

now = _broker.now

# Broker will only raise exceptions of type broker.Error
class Error(Exception):
  """A Broker-specific error."""
  pass

APIFlags = _broker.APIFlags
EC = _broker.EC
SC = _broker.SC
PeerStatus = _broker.PeerStatus
PeerFlags = _broker.PeerFlags
Frontend = _broker.Frontend
Backend = _broker.Backend
NetworkInfo = _broker.NetworkInfo
PeerInfo = _broker.PeerInfo
Topic = _broker.Topic

Address = _broker.Address
Count = _broker.Count
Enum = _broker.Enum
Port = _broker.Port
Set = _broker.Set
Subnet = _broker.Subnet
Table = _broker.Table
Timespan = _broker.Timespan
Timestamp = _broker.Timestamp
Vector = _broker.Vector

class Data(_broker.Data):
    def __init__(self, x = None):
        if x is None:
            _broker.Data.__init__(self)

        elif isinstance(x, _broker.Data):
            _broker.Data.__init__(self, x)

        elif isinstance(x, (bool, int, float, str,
                            Address, Count, Enum, Port, Set, Subnet, Table, Timespan, Timestamp, Vector)):
            _broker.Data.__init__(self, x)

        elif isinstance(x, datetime.timedelta):
            us = x.microseconds + (x.seconds + x.days * 24 * 3600) * 10**6
            ns = us * 10**3
            _broker.Data.__init__(self, _broker.Timespan(ns))

        elif isinstance(x, datetime.datetime):
            time_since_epoch = (x - datetime.datetime(1970, 1, 1)).total_seconds()
            _broker.Data.__init__(self, _broker.Timestamp(time_since_epoch))

        elif isinstance(x, ipaddress.IPv4Address):
            _broker.Data.__init__(self, _broker.Address(x.packed, 4))

        elif isinstance(x, ipaddress.IPv6Address):
            _broker.Data.__init__(self, _broker.Address(x.packed, 6))

        elif isinstance(x, ipaddress.IPv4Network):
            address = _broker.Address(x.network_address.packed, 4)
            length = x.prefixlen
            _broker.Data.__init__(self, _broker.Subnet(address, length))

        elif isinstance(x, ipaddress.IPv6Network):
            address = _broker.Address(x.network_address.packed, 6)
            length = x.prefixlen
            _broker.Data.__init__(self, _broker.Subnet(address, length))

        elif isinstance(x, list):
            v = _broker.Vector([Data(i) for i in x])
            _broker.Data.__init__(self, v)

        elif isinstance(x, set):
            s = _broker.Set(([Data(i) for i in x]))
            _broker.Data.__init__(self, s)

        elif isinstance(x, dict):
            t = _broker.Table()
            for (k, v) in x.items():
                t[Data(k)] = Data(v)

            _broker.Data.__init__(self, t)

        else:
            raise Error("unsupported data type: " + str(type(x)))

    @staticmethod
    def from_py(self, x):
        return Data(x)

    @staticmethod
    def to_py(d):
        def to_ipaddress(a):
            if a.is_v4():
                return ipaddress.IPv4Address(a.bytes()[-4:])
            else:
                return ipaddress.IPv6Address(a.bytes())

        def to_subnet(s):
            # Python < 3.5 does not have a nicer way of setting the prefixlen
            # when creating from packed data.
            if s.network().is_v4():
                return ipaddress.IPv4Network(to_ipaddress(s.network())).supernet(new_prefix=s.length())
            else:
                return ipaddress.IPv6Network(to_ipaddress(s.network())).supernet(new_prefix=s.length())

        def to_set(s):
            return set([Data.to_py(i) for i in s])

        def to_table(t):
            return {Data.to_py(k): Data.to_py(v) for (k, v) in t.items()}

        def to_vector(v):
            return [Data.to_py(i) for i in v]

        converters = {
            Data.Type.Address: lambda: to_ipaddress(d.as_address()),
            Data.Type.Boolean: lambda: d.as_boolean(),
            Data.Type.Count: lambda: Count(d.as_count()),
            Data.Type.EnumValue: lambda: d.as_enum_value(),
            Data.Type.Integer: lambda: d.as_integer(),
            Data.Type.Port: lambda: d.as_port(),
            Data.Type.Real: lambda: d.as_real(),
            Data.Type.Set: lambda: to_set(d.as_set()),
            Data.Type.String: lambda: d.as_string(),
            Data.Type.Subnet: lambda: to_subnet(d.as_subnet()),
            Data.Type.Table: lambda: to_table(d.as_table()),
            Data.Type.Timespan: lambda: d.as_timespan(),
            Data.Type.Timestamp: lambda: d.as_timestamp(),
            Data.Type.Vector: lambda: to_vector(d.as_vector())
            }

        try:
            return converters[d.get_type()]()
        except KeyError:
            raise Error("unsupported data type: " + str(d.get_type()))

####### TODO: Updated to new Broker API until here.

class Data__OLD__:
  def __init__(self, x = None):
    if x is None:
      self.data = _broker.Data()
    elif isinstance(x, (bool, int, float, str, Count, Timespan, Timestamp,
Port)):
      self.data = _broker.Data(x)
    elif isinstance(x, datetime.timedelta):
      us = x.microseconds + (x.seconds + x.days * 24 * 3600) * 10**6
      ns = us * 10**3
      self.data = _broker.Timespan(ns)
    elif isinstance(x, datetime.datetime):
      #self.data = _broker.Timestamp(x.timestamp()) # Python 3 only
      time_since_epoch = (x - datetime.datetime(1970, 1, 1)).total_seconds()
      self.data = _broker.Timestamp(time_since_epoch)
    elif isinstance(x, ipaddress.IPv4Address):
      self.data = _broker.Data(_broker.Address(x.packed, 4))
    elif isinstance(x, ipaddress.IPv6Address):
      self.data = _broker.Data(_broker.Address(x.packed, 6))
    elif isinstance(x, ipaddress.IPv4Network):
      address = _broker.Address(x.network_address.packed, 4)
      length = x.prefixlen
      self.data = _broker.Data(_broker.Subnet(address, length))
    elif isinstance(x, ipaddress.IPv6Network):
      address = _broker.Address(x.network_address.packed, 6)
      length = x.prefixlen
      self.data = _broker.Data(_broker.Subnet(address, length))
    elif isinstance(x, list):
      v = _broker.Vector(list(map(lambda d: Data(d).get(), x)))
      self.data = _broker.Data(v)
    elif isinstance(x, set):
      s = _broker.Set(list(map(lambda d: Data(d).get(), x)))
      self.data = _broker.Data(s)
    elif isinstance(x, dict):
      t = _broker.Table({Data(k).get(): Data(v).get() for k, v in x.items()})
      self.data = _broker.Data(t)
    else:
      raise BrokerError("unsupported data type: " + str(type(x)))

  def get(self):
    return self.data

  def __eq__(self, other):
    if isinstance(other, Data):
      return self.data == other.data
    elif isinstance(other, _broker.Data):
      return self.data == other
    else:
      return self == Data(other)

  def __ne__(self, other):
    return not self.__eq__(other)

  def __str__(self):
    return str(self.data)

# TODO: complete interface
class Store:
  def __init__(self, handle):
    self.store = handle

  def name(self):
    return self.store.name()


class Endpoint:
   def __init__(self, handle):
     self.endpoint = handle

   def listen(self, address = "", port = 0):
     return self.endpoint.listen(address, port)

   def peer(self, other):
     self.endpoint.peer(other.endpoint)

   def remote_peer(self, address, port):
     self.endpoint.peer(str(address), port)

   def unpeer(self, other):
     self.endpoint.unpeer(other)

   def remote_unpeer(self, address, port):
     self.endpoint.unpeer(str(address), port)

   def publish(self, topic, data):
     x = data if isinstance(data, Data) else Data(data)
     self.endpoint.publish(topic, x.get())

   def attach(self, frontend, name, backend = None, backend_options = None):
     if frontend == Frontend.Clone:
       return self.endpoint.attach_clone(name)
     else:
       return self.endpoint.attach_master(name, backend, backend_options)

class Mailbox:
  def __init__(self, handle):
    self.mailbox = handle

  def descriptor(self):
    return self.mailbox.descriptor()

  def empty(self):
    return self.mailbox.empty()

  def count(self, n = -1):
    return self.mailbox.count(n)


class Message:
  def __init__(self, handle):
    self.message = handle

  def topic(self):
    return self.message.topic().string()

  def data(self):
    return self.message.data() # TODO: unwrap properly

  def __str__(self):
    return "%s -> %s" % (self.topic(), str(self.data()))


class BlockingEndpoint(Endpoint):
  def __init__(self, handle):
    super(BlockingEndpoint, self).__init__(handle)

  def subscribe(self, topic):
    self.endpoint.subscribe(topic)

  def unsubscribe(self, topic):
    self.endpoint.unsubscribe(topic)

  def receive(self, x):
    if x == Status:
      return self.endpoint.receive()
    elif x == Message:
      return Message(self.endpoint.receive())
    else:
      raise BrokerError("invalid receive type")

  #def receive(self):
  #  if fun1 is None:
  #    return Message(self.endpoint.receive())
  #  if fun2 is None:
  #    if utils.arity(fun1) == 1:
  #      return self.endpoint.receive_status(fun1)
  #    if utils.arity(fun1) == 2:
  #      return self.endpoint.receive_msg(fun1)
  #    raise BrokerError("invalid receive callback arity; must be 1 or 2")
  #  return self.endpoint.receive_msg_or_status(fun1, fun2)

  def mailbox(self):
    return Mailbox(self.endpoint.mailbox())


class NonblockingEndpoint(Endpoint):
  def __init__(self, handle):
    super(NonblockingEndpoint, self).__init__(handle)

  def subscribe(self, topic, fun):
    self.endpoint.subscribe_msg(topic, fun)

  def on_status(fun):
    self.endpoint.subscribe_status(fun)

  def unsubscribe(self, topic):
    self.endpoint.unsubscribe(topic)


class Context:
  def __init__(self):
    self.context = _broker.Context()

  def spawn(self, api):
    if api == Blocking:
      return BlockingEndpoint(self.context.spawn_blocking())
    elif api == Nonblocking:
      return NonblockingEndpoint(self.context.spawn_nonblocking())
    else:
      raise BrokerError("invalid API flag: " + str(api))
