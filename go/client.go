// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Robin Jarry

// Package grout provides a pure Go client library for the grout network
// router API.
//
// The client communicates with the grout daemon over a UNIX socket using
// the native binary protocol. No CGO or C compiler required.
//
// A [Client] is NOT safe for concurrent use. Use one Client per
// goroutine, or protect with external synchronization.
package grout

import (
	"encoding/binary"
	"io"
	"iter"
	"net"
	"sync/atomic"
	"syscall"
)

const helloVersion = "go-grout"

type cachedResponse struct {
	forID      uint32
	status     uint32
	payloadLen uint32
	payload    []byte
}

type Client struct {
	conn   net.Conn
	nextID atomic.Uint32
	cached []cachedResponse
}

func Connect(sockPath string) (*Client, error) {
	if sockPath == "" {
		sockPath = DefaultSockPath
	}
	conn, err := net.Dial("unix", sockPath)
	if err != nil {
		return nil, err
	}

	c := &Client{conn: conn}

	if err := c.hello(); err != nil {
		_ = conn.Close()
		return nil, err
	}

	return c, nil
}

func (c *Client) Close() error {
	if c.conn == nil {
		return nil
	}
	err := c.conn.Close()
	c.conn = nil
	return err
}

func (c *Client) hello() error {
	var req [4 + 128]byte
	binary.NativeEndian.PutUint32(req[0:4], ApiVersion)
	copy(req[4:], helloVersion)

	_, err := c.sendRecv(grHello, req[:])
	return err
}

func (c *Client) send(reqType uint32, payload []byte) (uint32, error) {
	id := c.nextID.Add(1)

	var hdr [reqHeaderSize]byte
	binary.NativeEndian.PutUint32(hdr[0:4], id)
	binary.NativeEndian.PutUint32(hdr[4:8], reqType)
	binary.NativeEndian.PutUint32(hdr[8:12], uint32(len(payload)))

	if _, err := c.conn.Write(hdr[:]); err != nil {
		return 0, &Error{Errno: syscall.EIO, Op: "send"}
	}
	if len(payload) > 0 {
		if _, err := c.conn.Write(payload); err != nil {
			return 0, &Error{Errno: syscall.EIO, Op: "send"}
		}
	}
	return id, nil
}

func (c *Client) recv(wantID uint32) ([]byte, error) {
	// Check for a cached response with the requested ID.
	for i, cr := range c.cached {
		if cr.forID == wantID {
			c.cached = append(c.cached[:i], c.cached[i+1:]...)
			return cr.payload, statusError(cr.status)
		}
	}

	for {
		forID, status, payload, err := c.recvOne()
		if err != nil {
			return nil, err
		}
		if forID == wantID {
			return payload, statusError(status)
		}
		c.cached = append(c.cached, cachedResponse{
			forID: forID, status: status, payload: payload,
		})
	}
}

func (c *Client) recvOne() (forID, status uint32, payload []byte, err error) {
	var hdr [respHeaderSize]byte
	if _, err = io.ReadFull(c.conn, hdr[:]); err != nil {
		return 0, 0, nil, &Error{Errno: syscall.EIO, Op: "recv"}
	}

	forID = binary.NativeEndian.Uint32(hdr[0:4])
	status = binary.NativeEndian.Uint32(hdr[4:8])
	payloadLen := binary.NativeEndian.Uint32(hdr[8:12])

	if payloadLen > ApiMaxMsgLen {
		return 0, 0, nil, &Error{Errno: syscall.EMSGSIZE, Op: "recv"}
	}
	if payloadLen > 0 {
		payload = make([]byte, payloadLen)
		if _, err = io.ReadFull(c.conn, payload); err != nil {
			return 0, 0, nil, &Error{Errno: syscall.EIO, Op: "recv"}
		}
	}
	return forID, status, payload, nil
}

func statusError(status uint32) error {
	if status == 0 {
		return nil
	}
	return &Error{Errno: syscall.Errno(status), Op: "recv"}
}

func (c *Client) sendRecv(reqType uint32, payload []byte) ([]byte, error) {
	id, err := c.send(reqType, payload)
	if err != nil {
		return nil, err
	}
	return c.recv(id)
}

func (c *Client) sendOnly(reqType uint32, payload []byte) error {
	_, err := c.sendRecv(reqType, payload)
	return err
}

func streamIter[T any, PT interface {
	*T
	unmarshaler
}](c *Client, reqType uint32, payload []byte) iter.Seq2[*T, error] {
	return func(yield func(*T, error) bool) {
		id, err := c.send(reqType, payload)
		if err != nil {
			yield(nil, err)
			return
		}
		for {
			data, err := c.recv(id)
			if err != nil {
				yield(nil, err)
				return
			}
			if data == nil {
				return
			}
			v := new(T)
			if err := PT(v).unmarshal(data); err != nil {
				yield(nil, err)
				return
			}
			if !yield(v, nil) {
				c.streamDrain(id)
				return
			}
		}
	}
}

func (c *Client) streamDrain(id uint32) {
	for {
		data, err := c.recv(id)
		if err != nil || data == nil {
			return
		}
	}
}
