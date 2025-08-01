import { NativeModules, Platform, NativeEventEmitter } from 'react-native';
import { EventEmitter } from 'tseep';
import { Buffer } from 'buffer';

const LINKING_ERROR =
  `The package 'react-native-jsi-udp' doesn't seem to be linked. Make sure: \n\n` +
  Platform.select({ ios: "- You have run 'pod install'\n", default: '' }) +
  '- You rebuilt the app after installing the package\n' +
  '- You are not using Expo Go\n';

const JsiUdp = NativeModules.JsiUdp
  ? NativeModules.JsiUdp
  : new Proxy(
      {},
      {
        get() {
          throw new Error(LINKING_ERROR);
        },
      }
    );

// Create a NativeEventEmitter specifically for the JsiUdp module
const udpEventEmitter = new NativeEventEmitter(JsiUdp);
class UdpEventManager extends EventEmitter {
  private static instance: UdpEventManager | null = null;
  private initialized = false;

  static getInstance(): UdpEventManager {
    if (!UdpEventManager.instance) {
      UdpEventManager.instance = new UdpEventManager();
    }
    return UdpEventManager.instance;
  }

  private constructor() {
    super();
    this.initialize();
  }

  private initialize() {
    if (this.initialized) return;

    // Single root handler for ALL UDP messages
    udpEventEmitter.addListener('udp_message', (event) => {
      // Emit socket-specific event internally
      this.emit(`socket_${event.socketId}_message`, event);
    });

    this.initialized = true;
  }

  // Method to get socket-specific events
  onSocketMessage(socketId: number, callback: (event: any) => void) {
    this.on(`socket_${socketId}_message`, callback);
  }

  removeSocketListeners(socketId: number) {
    this.removeAllListeners(`socket_${socketId}_message`);
  }
}

// Global singleton instance
const udpManager = UdpEventManager.getInstance();

export interface Options {
  type: 'udp4' | 'udp6';
  reuseAddr?: boolean;
  reusePort?: boolean;
}

export enum State {
  UNBOUND = 0,
  BOUND = 1,
  CLOSED = 2,
}

export type Callback = (...args: any[]) => void;

export class Socket extends EventEmitter {
  private state: State;
  private type: 4 | 6;
  private _id: number;
  private reuseAddr: boolean;
  private reusePort: boolean;
  private _receiving: boolean = false;
  private _timeoutId: ReturnType<typeof setTimeout> | null = null;

  constructor(options: Options, callback?: Callback) {
    super();
    if (typeof datagram_create !== 'function') {
      JsiUdp.install();
    }
    this.state = State.UNBOUND;
    this.type = options.type === 'udp4' ? 4 : 6;
    this.reuseAddr = options.reuseAddr ?? false;
    this.reusePort = options.reusePort ?? false;
    this._id = datagram_create(this.type);
    if (callback) this.on('message', callback);
  }

  private startReceiving() {
    if (this._receiving || this.state !== State.BOUND) return;

    udpManager.onSocketMessage(this._id, (event) => {
      console.log('🔵 Received message for socket:', this._id, event);
      if (event?.type === 'message') {
        const buffer = event.data
          ? Buffer.from(event.data, 'base64')
          : Buffer.alloc(0);
        this.emit('message', buffer, {
          address: event.address,
          port: event.port,
          family: event.family,
        });
      } else if (event?.type === 'error') {
        this.emit('error', event.error);
      }
    });
    datagram_receive(this._id);
    this._receiving = true;
  }

  private stopReceiving() {
    if (!this._receiving) return;

    this._receiving = false;
    if (this._timeoutId) {
      clearTimeout(this._timeoutId);
      this._timeoutId = null;
    }

    // Remove event listeners first
    udpManager.removeSocketListeners(this._id);

    // Stop the background receive thread
    if (typeof datagram_stop_receive === 'function') {
      datagram_stop_receive(this._id);
    }
  }

  bind(port?: number, address?: string | Callback, callback?: Callback) {
    if (this.state !== State.UNBOUND) {
      throw new Error('Socket is already bound');
    }
    if (typeof address === 'function') {
      callback = address;
      address = undefined;
    }
    if (callback) this.once('listening', callback);
    const defaultAddr = this.type === 4 ? '0.0.0.0' : '::1';
    try {
      datagram_setOpt(
        this._id,
        dgc_SOL_SOCKET,
        dgc_SO_REUSEADDR,
        this.reuseAddr ? 1 : 0
      );
      datagram_setOpt(
        this._id,
        dgc_SOL_SOCKET,
        dgc_SO_REUSEPORT,
        this.reusePort ? 1 : 0
      );
      datagram_bind(this._id, this.type, address ?? defaultAddr, port ?? 0);
      this.state = State.BOUND;
      this.emit('listening');
      this.startReceiving();
    } catch (e) {
      if (callback) callback(e as Error);
      else this.emit('error', e);
    }
  }

  send(
    data: string | Buffer,
    offset: number | undefined,
    length: number | undefined,
    port: number,
    address: string,
    callback?: (e?: Error) => void
  ) {
    let buf: Buffer;
    if (typeof data === 'string') {
      buf = Buffer.from(data);
    } else {
      buf = data;
    }
    buf = buf.slice(offset ?? 0, length ?? buf.length);
    try {
      datagram_send(
        this._id,
        this.type,
        address,
        port,
        buf.buffer as ArrayBuffer
      );
      callback?.();
    } catch (e) {
      if (callback) callback(e as Error);
      else this.emit('error', e);
    }
  }

  close(callback?: Callback) {
    if (this.state === State.CLOSED) {
      return;
    }
    if (callback) this.once('close', callback!);
    this.stopReceiving();
    datagram_close(this._id);
    this.state = State.CLOSED;
    this.emit('close');
  }

  address() {
    return datagram_getSockName(this._id, this.type);
  }

  setBroadcast(flag: boolean) {
    datagram_setOpt(this._id, dgc_SOL_SOCKET, dgc_SO_BROADCAST, flag ? 1 : 0);
  }

  getRecvBufferSize() {
    return datagram_getOpt(this._id, dgc_SOL_SOCKET, dgc_SO_RCVBUF);
  }

  setRecvBufferSize(size: number) {
    datagram_setOpt(this._id, dgc_SOL_SOCKET, dgc_SO_RCVBUF, size);
  }

  getSendBufferSize() {
    return datagram_getOpt(this._id, dgc_SOL_SOCKET, dgc_SO_SNDBUF);
  }

  setSendBufferSize(size: number) {
    datagram_setOpt(this._id, dgc_SOL_SOCKET, dgc_SO_SNDBUF, size);
  }

  addMembership(multicastAddress: string, multicastInterface?: string) {
    datagram_setOpt(
      this._id,
      this.type === 4 ? dgc_IPPROTO_IP : dgc_IPPROTO_IPV6,
      dgc_IP_ADD_MEMBERSHIP,
      multicastAddress,
      multicastInterface
    );
  }

  dropMembership(multicastAddress: string, multicastInterface?: string) {
    datagram_setOpt(
      this._id,
      this.type === 4 ? dgc_IPPROTO_IP : dgc_IPPROTO_IPV6,
      dgc_IP_DROP_MEMBERSHIP,
      multicastAddress,
      multicastInterface
    );
  }

  setMulticastTTL(ttl: number) {
    datagram_setOpt(
      this._id,
      this.type === 4 ? dgc_IPPROTO_IP : dgc_IPPROTO_IPV6,
      dgc_IP_MULTICAST_TTL,
      ttl
    );
  }

  setMulticastLoopback(flag: boolean) {
    datagram_setOpt(
      this._id,
      this.type === 4 ? dgc_IPPROTO_IP : dgc_IPPROTO_IPV6,
      dgc_IP_MULTICAST_LOOP,
      flag ? 1 : 0
    );
  }

  setTTL(ttl: number) {
    datagram_setOpt(
      this._id,
      this.type === 4 ? dgc_IPPROTO_IP : dgc_IPPROTO_IPV6,
      dgc_IP_TTL,
      ttl
    );
  }

  ref() {
    return this; // Not implemented
  }

  unref() {
    return this; // Not implemented
  }
}

export function createSocket(options: Options | 'udp4' | 'udp6') {
  if (typeof options === 'string') {
    options = { type: options };
  }
  return new Socket(options);
}

export default {
  createSocket,
  Socket,
};
