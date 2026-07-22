# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
ONVIF Profile S §7.7 — Event handling for *clients*.

Implements the client-side requirements listed in
*ONVIF Profile S Specification v1.3, §7.7.4 — Event Handling Function
List for Clients*:

==================================  =================  ============
Function                            Service            Requirement
==================================  =================  ============
Notify                              Event              (Push path)
Subscribe                           Event              M*
Renew                               Event              M
Unsubscribe                         Event              O
SetSynchronizationPoint             Event              O
CreatePullPointSubscription         Event              M*
PullMessages                        Event              M (pull path)
GetEventProperties                  Event              O
TopicFilter                         Event              O
MessageContentFilter                Event              O
==================================  =================  ============

\\* At least one of *Subscribe* (Base-Notification push) or
*CreatePullPointSubscription* (PullPoint pull) MUST be implemented.
This module implements **both** paths:

* the PullPoint pull path — :class:`OnvifPullPointSubscription`
  (Create / Pull / Renew / Unsubscribe / SetSynchronizationPoint);
* the Base-Notification push path —
  :meth:`OnvifEventClient.subscribe_base_notification`,
  :meth:`OnvifEventClient.renew_base_subscription`,
  :meth:`OnvifEventClient.unsubscribe_base_subscription`,
  plus :class:`NotificationConsumerServer` (the embedded HTTP endpoint
  that receives ``Notify`` SOAP POSTs) and :class:`NotifyListener`
  (a daemon-thread combining Server + Subscribe + auto-Renew).

Public API
----------
- :class:`OnvifEventClient` — facade over an :class:`onvif.ONVIFCamera`
  exposing the spec functions.
- :class:`OnvifPullPointSubscription` — context-manager wrapping one
  pull-point subscription (Create/Pull/Renew/Unsubscribe).
- :class:`OnvifEventListener` — background daemon-thread PullPoint pump.
- :class:`NotificationConsumerServer` — HTTP endpoint for ``Notify``.
- :class:`NotifyListener` — daemon-thread Base-Notification listener
  (Subscribe + ConsumerServer + auto-Renew + Unsubscribe).
- :class:`OnvifEvent` — parsed notification message
  (Topic, Source, Data, UtcTime, PropertyOperation).
- :data:`EVENT_TOPICS` — well-known Profile-S topic constants
  (MotionAlarm, Tamper, GlobalSceneChange…).
"""
from __future__ import annotations

import socket
import threading
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from types import SimpleNamespace
from typing import Any, Callable, Iterator, Optional

try:
    from lxml import etree as _etree  # type: ignore[import-not-found]
    _HAS_LXML = True
except ImportError:  # pragma: no cover
    _HAS_LXML = False

from onvif import ONVIFCamera  # pylint: disable=import-error


# ---------------------------------------------------------------------------
# Well-known Profile-S topics (§Profile S §7.7 baseline events)
# ---------------------------------------------------------------------------

EVENT_TOPICS = SimpleNamespace(
    #: ``tns1:VideoSource/MotionAlarm`` — basic motion detection event
    #: that every Profile-S device that signals motion MUST publish.
    MOTION_ALARM="tns1:VideoSource/MotionAlarm",
    #: ``tns1:VideoSource/GlobalSceneChange/ImagingService`` —
    #: large-scale scene change (camera moved, covered, etc.).
    GLOBAL_SCENE_CHANGE="tns1:VideoSource/GlobalSceneChange/ImagingService",
    #: Camera tamper detected.
    TAMPER="tns1:VideoSource/Tamper",
    #: Wildcard subtree filter: every event under ``tns1:VideoSource``.
    ANY_VIDEO_SOURCE="tns1:VideoSource//.",
    #: Wildcard filter matching every event topic.
    ANY=".//.",
)

#: TopicFilter dialect understood by every ONVIF device.
TOPIC_DIALECT_CONCRETE = (
    "http://docs.oasis-open.org/wsn/t-1/TopicExpression/Concrete"
)


# ---------------------------------------------------------------------------
# Parsed event model
# ---------------------------------------------------------------------------

@dataclass
class OnvifEvent:
    """A single notification message decoded from a PullMessages response."""

    topic: str = ""
    utc_time: Optional[datetime] = None
    property_operation: str = ""    # ``Initialized`` / ``Changed`` / ``Deleted``
    source: dict[str, str] = field(default_factory=dict)
    data: dict[str, Any] = field(default_factory=dict)
    subscription_reference: str = ""
    producer_reference: str = ""

    def __str__(self) -> str:
        when = self.utc_time.isoformat(timespec="seconds") if self.utc_time else "?"
        src = ",".join(f"{k}={v}" for k, v in self.source.items()) or "-"
        dat = ",".join(f"{k}={v}" for k, v in self.data.items()) or "-"
        op = self.property_operation or "-"
        return f"[{when}] {self.topic}  op={op}  src=({src})  data=({dat})"


# ---------------------------------------------------------------------------
# Notification-message decoding helpers (zeep returns lxml elements for xsd:any)
# ---------------------------------------------------------------------------

_NS_TT = "http://www.onvif.org/ver10/schema"
_NS_WSNT = "http://docs.oasis-open.org/wsn/b-2"


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1] if "}" in tag else tag


def _parse_iso_utc(s: str) -> Optional[datetime]:
    if not s:
        return None
    try:
        if s.endswith("Z"):
            s = s[:-1] + "+00:00"
        return datetime.fromisoformat(s).astimezone(timezone.utc)
    except (ValueError, TypeError):
        return None


def _decode_simple_items(items_element: Any) -> dict[str, str]:
    """Decode a ``<tt:Source>`` / ``<tt:Data>`` block into ``{Name: Value}``."""
    out: dict[str, str] = {}
    if items_element is None:
        return out
    for child in items_element:
        if _local(child.tag) != "SimpleItem":
            continue
        name = child.get("Name") or ""
        value = child.get("Value") or ""
        if name:
            out[name] = value
    return out


def _str(value: Any) -> str:
    if value is None:
        return ""
    inner = getattr(value, "_value_1", None)
    if inner is not None:
        return str(inner)
    return str(value)


def _parse_notification_message(notif: Any) -> OnvifEvent:
    """Convert one zeep ``NotificationMessage`` into :class:`OnvifEvent`."""
    ev = OnvifEvent(
        topic=_str(getattr(notif, "Topic", "")),
        producer_reference=_str(
            getattr(getattr(notif, "ProducerReference", None), "Address", "")
        ),
        subscription_reference=_str(
            getattr(getattr(notif, "SubscriptionReference", None), "Address", "")
        ),
    )

    message_wrapper = getattr(notif, "Message", None)
    inner = getattr(message_wrapper, "_value_1", message_wrapper)
    if inner is None or not _HAS_LXML:
        return ev

    # `inner` is an lxml.etree._Element corresponding to <tt:Message>
    if hasattr(inner, "get"):
        ev.utc_time = _parse_iso_utc(inner.get("UtcTime") or "")
        ev.property_operation = inner.get("PropertyOperation") or ""

        source_el = inner.find(f"{{{_NS_TT}}}Source")
        data_el = inner.find(f"{{{_NS_TT}}}Data")
        ev.source = _decode_simple_items(source_el)
        ev.data = _decode_simple_items(data_el)
    return ev


# ---------------------------------------------------------------------------
# Pull-point subscription
# ---------------------------------------------------------------------------

class OnvifPullPointSubscription:
    """One ONVIF *PullPoint* subscription.

    Wraps the spec functions:

    - :meth:`create`                 → ``CreatePullPointSubscription`` (M\\*)
    - :meth:`pull`                   → ``PullMessages``                 (M)
    - :meth:`renew`                  → ``Renew``                        (M)
    - :meth:`unsubscribe`            → ``Unsubscribe``                  (O)
    - :meth:`set_synchronization_point`
                                     → ``SetSynchronizationPoint``      (O)

    Use as a context manager to guarantee that ``Unsubscribe`` is sent
    when the block exits.
    """

    def __init__(
        self,
        camera: ONVIFCamera,
        *,
        topic_filter: Optional[str] = None,
        message_content_filter: Optional[str] = None,
        initial_termination_time: str = "PT1M",
    ) -> None:
        self._camera = camera
        self._topic_filter = topic_filter
        self._message_filter = message_content_filter
        self._initial_termination_time = initial_termination_time
        self._pullpoint_service: Any = None
        self._subscription_reference: str = ""
        self._lock = threading.RLock()

    # ---- spec ops --------------------------------------------------------

    def create(self) -> "OnvifPullPointSubscription":
        """Send ``CreatePullPointSubscription`` and build the bound pull-point
        service. Idempotent — calling twice replaces the previous binding."""
        with self._lock:
            events = self._camera.create_events_service()
            req = events.create_type("CreatePullPointSubscription")
            req.InitialTerminationTime = self._initial_termination_time
            if self._topic_filter:
                req.Filter = {
                    "TopicExpression": {
                        "_value_1": self._topic_filter,
                        "Dialect": TOPIC_DIALECT_CONCRETE,
                    }
                }
            if self._message_filter:
                # MessageContentFilter is OPTIONAL (§7.7.4) — populated only
                # when caller explicitly opts in.
                req.Filter = req.Filter or {}
                req.Filter["MessageContent"] = {
                    "_value_1": self._message_filter,
                    "Dialect": (
                        "http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter"
                    ),
                }
            response = events.CreatePullPointSubscription(req)
            self._subscription_reference = _str(
                getattr(response.SubscriptionReference, "Address", "")
            )
            # Bind a dedicated PullPoint service to the subscription URL.
            self._pullpoint_service = self._camera.create_pullpoint_service()
            self._pullpoint_service.url = self._subscription_reference
            self._pullpoint_service.xaddr = self._subscription_reference
            if hasattr(self._pullpoint_service, "ws_client"):
                try:
                    self._pullpoint_service.ws_client.set_options(
                        location=self._subscription_reference
                    )
                except Exception:  # pylint: disable=broad-exception-caught
                    pass
            return self

    def pull(
        self,
        *,
        timeout: str = "PT5S",
        max_messages: int = 32,
    ) -> list[OnvifEvent]:
        """Send ``PullMessages`` (§7.7.4 M). Blocks for at most *timeout*.

        Parameters
        ----------
        timeout:
            ISO-8601 duration string (default ``"PT5S"``); how long the
            device may wait for an event before returning empty.
        max_messages:
            Hard cap on the number of messages returned per call.
        """
        self._require_active()
        req = self._pullpoint_service.create_type("PullMessages")
        req.Timeout = timeout
        req.MessageLimit = max_messages
        response = self._pullpoint_service.PullMessages(req)
        notifications = getattr(response, "NotificationMessage", None) or []
        return [_parse_notification_message(n) for n in notifications]

    def renew(self, termination_time: str = "PT1M") -> None:
        """Send ``Renew`` (§7.7.4 M) to extend the subscription lifetime."""
        self._require_active()
        req = self._pullpoint_service.create_type("Renew")
        req.TerminationTime = termination_time
        self._pullpoint_service.Renew(req)

    def unsubscribe(self) -> None:
        """Send ``Unsubscribe`` (§7.7.4 O) and forget the subscription."""
        with self._lock:
            svc = self._pullpoint_service
            self._pullpoint_service = None
            self._subscription_reference = ""
            if svc is None:
                return
            try:
                svc.Unsubscribe()
            except Exception:  # pylint: disable=broad-exception-caught
                # Best-effort: device may have already expired the subscription.
                pass

    def set_synchronization_point(self) -> None:
        """Send ``SetSynchronizationPoint`` (§7.7.4 O) — asks the device to
        republish the current state of every property event so the client
        can resync without waiting for the next change."""
        self._require_active()
        try:
            self._pullpoint_service.SetSynchronizationPoint()
        except Exception:  # pylint: disable=broad-exception-caught
            # Optional per spec; some devices refuse it.
            pass

    # ---- properties ------------------------------------------------------

    @property
    def subscription_reference(self) -> str:
        return self._subscription_reference

    @property
    def is_active(self) -> bool:
        return self._pullpoint_service is not None

    # ---- context manager -------------------------------------------------

    def __enter__(self) -> "OnvifPullPointSubscription":
        return self.create()

    def __exit__(self, *_exc: Any) -> None:
        self.unsubscribe()

    # ---- internal --------------------------------------------------------

    def _require_active(self) -> None:
        if self._pullpoint_service is None:
            raise RuntimeError(
                "OnvifPullPointSubscription is not active — call create() first"
            )


# ---------------------------------------------------------------------------
# High-level client facade
# ---------------------------------------------------------------------------

class OnvifEventClient:
    """Facade implementing the §7.7.4 client checklist over a single camera.

    Construct from a camera entry::

        client = OnvifEventClient.from_camera_entry(entry)

    or from raw connection parameters::

        client = OnvifEventClient("10.0.0.10", 80, "user", "pass")

    Then either run the simple polling loop::

        for event in client.iter_events(topic_filter=EVENT_TOPICS.MOTION_ALARM):
            print(event)

    or open a managed subscription::

        with client.create_pull_point() as sub:
            for _ in range(10):
                for event in sub.pull(timeout="PT5S"):
                    print(event)
                sub.renew("PT1M")
    """

    def __init__(
        self,
        host: Optional[str] = None,
        port: int = 80,
        user: str = "",
        password: str = "",
        *,
        camera: Optional[ONVIFCamera] = None,
    ) -> None:
        if camera is None:
            if host is None:
                raise ValueError("either `host` or `camera` must be supplied")
            camera = ONVIFCamera(host, port, user, password)
        self._camera = camera

    @classmethod
    def from_camera_entry(cls, entry: Any) -> "OnvifEventClient":
        """Build a client from a :class:`DlsOnvifCameraEntry`."""
        return cls(
            host=entry.hostname,
            port=int(entry.port or 80),
            user=entry.username or "",
            password=entry.password or "",
        )

    # ---- service capability ---------------------------------------------

    def supports_events(self) -> bool:
        """Return ``True`` when the device advertises the Event service.

        Profile S §7.7.1 mandates the service on devices, but legacy or
        partial implementations sometimes omit it; clients must degrade
        gracefully.
        """
        try:
            self._camera.create_events_service()
            return True
        except Exception:  # pylint: disable=broad-exception-caught
            return False

    # ---- §7.7.4 operations ----------------------------------------------

    def get_event_properties(self) -> dict[str, Any]:
        """Send ``GetEventProperties`` (§7.7.4 O).

        Returns a dict with the keys ``TopicNamespaceLocation``,
        ``TopicSet``, ``TopicExpressionDialect``,
        ``MessageContentFilterDialect`` so callers can inspect what
        topics and filter dialects the device supports before
        subscribing.
        """
        events = self._camera.create_events_service()
        response = events.GetEventProperties()
        return {
            "TopicNamespaceLocation": getattr(
                response, "TopicNamespaceLocation", []
            ) or [],
            "FixedTopicSet": bool(getattr(response, "FixedTopicSet", False)),
            "TopicSet": getattr(response, "TopicSet", None),
            "TopicExpressionDialect": list(
                getattr(response, "TopicExpressionDialect", []) or []
            ),
            "MessageContentFilterDialect": list(
                getattr(response, "MessageContentFilterDialect", []) or []
            ),
        }

    def create_pull_point(
        self,
        *,
        topic_filter: Optional[str] = None,
        message_content_filter: Optional[str] = None,
        initial_termination_time: str = "PT1M",
    ) -> OnvifPullPointSubscription:
        """Build (but do not start) a :class:`OnvifPullPointSubscription`.

        Use as a context manager to auto-create on entry and
        auto-unsubscribe on exit.
        """
        return OnvifPullPointSubscription(
            self._camera,
            topic_filter=topic_filter,
            message_content_filter=message_content_filter,
            initial_termination_time=initial_termination_time,
        )

    def subscribe_base_notification(
        self,
        *,
        consumer_reference: str,
        topic_filter: Optional[str] = None,
        initial_termination_time: str = "PT1M",
    ) -> Any:
        """Send ``Subscribe`` (§7.7.4 M\\*) — base-notification push path.

        Requires the caller to host a notification consumer at
        *consumer_reference* (an HTTP endpoint accepting ``Notify``
        SOAP messages). Returns the raw zeep response so the caller
        can extract the ``SubscriptionReference`` and call
        :meth:`renew_subscription` / :meth:`unsubscribe_subscription`.
        """
        events = self._camera.create_events_service()
        req = events.create_type("Subscribe")
        req.ConsumerReference = {"Address": consumer_reference}
        req.InitialTerminationTime = initial_termination_time
        if topic_filter:
            req.Filter = {
                "TopicExpression": {
                    "_value_1": topic_filter,
                    "Dialect": TOPIC_DIALECT_CONCRETE,
                }
            }
        return events.Subscribe(req)

    def renew_base_subscription(
        self,
        subscription_reference: str,
        termination_time: str = "PT1M",
    ) -> None:
        """Send ``Renew`` (§7.7.4 M) on a Base-Notification subscription.

        *subscription_reference* is the value returned by
        :meth:`subscribe_base_notification` (the ``Address`` field of
        the ``SubscriptionReference`` element).
        """
        svc = self._camera.create_subscription_service()
        svc.url = subscription_reference
        svc.xaddr = subscription_reference
        req = svc.create_type("Renew")
        req.TerminationTime = termination_time
        svc.Renew(req)

    def unsubscribe_base_subscription(self, subscription_reference: str) -> None:
        """Send ``Unsubscribe`` (§7.7.4 O) on a Base-Notification subscription.

        Best-effort: a device that already expired the subscription will
        return a SOAP fault, which is swallowed.
        """
        try:
            svc = self._camera.create_subscription_service()
            svc.url = subscription_reference
            svc.xaddr = subscription_reference
            svc.Unsubscribe()
        except Exception:  # pylint: disable=broad-exception-caught
            pass

    # ---- high-level pump ------------------------------------------------

    def iter_events(
        self,
        *,
        topic_filter: Optional[str] = None,
        pull_timeout: str = "PT5S",
        max_messages: int = 32,
        max_iterations: Optional[int] = None,
        renew_every: int = 5,
        renew_for: str = "PT2M",
        stop: Optional[threading.Event] = None,
    ) -> Iterator[OnvifEvent]:
        """Synchronous generator pulling events until *stop* or
        *max_iterations* is reached.

        Automatically renews the subscription every *renew_every* pulls.
        Each iteration calls :meth:`OnvifPullPointSubscription.pull`;
        every event the device returns is yielded one by one.
        """
        with self.create_pull_point(topic_filter=topic_filter) as sub:
            iteration = 0
            while True:
                if stop is not None and stop.is_set():
                    return
                if max_iterations is not None and iteration >= max_iterations:
                    return
                events = sub.pull(timeout=pull_timeout, max_messages=max_messages)
                for event in events:
                    yield event
                iteration += 1
                if iteration % renew_every == 0:
                    try:
                        sub.renew(renew_for)
                    except Exception:  # pylint: disable=broad-exception-caught
                        # Recreate on hard failure so the loop keeps running.
                        sub.unsubscribe()
                        sub.create()


# ---------------------------------------------------------------------------
# Convenience: spin a daemon thread that pumps events into a callback.
# ---------------------------------------------------------------------------

class OnvifEventListener:
    """Background pull-point pump.

    Spawns a daemon thread that calls *callback(event)* for every
    notification received. Stop the pump by calling :meth:`stop` —
    the wrapped subscription is unsubscribed cleanly.
    """

    def __init__(
        self,
        client: OnvifEventClient,
        callback: "Any",
        *,
        topic_filter: Optional[str] = None,
        pull_timeout: str = "PT5S",
    ) -> None:
        self._client = client
        self._callback = callback
        self._topic_filter = topic_filter
        self._pull_timeout = pull_timeout
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> "OnvifEventListener":
        if self._thread is not None and self._thread.is_alive():
            return self
        self._stop.clear()
        self._thread = threading.Thread(
            target=self._run, name="onvif-event-listener", daemon=True
        )
        self._thread.start()
        return self

    def _run(self) -> None:
        try:
            for event in self._client.iter_events(
                topic_filter=self._topic_filter,
                pull_timeout=self._pull_timeout,
                stop=self._stop,
            ):
                try:
                    self._callback(event)
                except Exception:  # pylint: disable=broad-exception-caught
                    # Never let user callback kill the pump.
                    pass
        except Exception:  # pylint: disable=broad-exception-caught
            pass

    def stop(self, *, timeout: float = 8.0) -> None:
        self._stop.set()
        thread = self._thread
        self._thread = None
        if thread is not None:
            thread.join(timeout=timeout)

    @property
    def is_running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()


# ---------------------------------------------------------------------------
# Notification consumer (§7.7.4 — push path: Notify)
# ---------------------------------------------------------------------------

class _NotifyHandler(BaseHTTPRequestHandler):
    """Embedded HTTP handler for incoming ``Notify`` SOAP POSTs.

    The owning :class:`NotificationConsumerServer` is reached through
    the ``server.notify_consumer`` attribute (set on construction).
    """

    # Silence the default per-request access log on stderr.
    def log_message(  # noqa: A002  pylint: disable=redefined-builtin
        self, format: str, *args: Any,
    ) -> None:
        return

    def do_POST(self) -> None:  # noqa: N802  (BaseHTTPRequestHandler API)
        length = int(self.headers.get("Content-Length") or "0")
        body = self.rfile.read(length) if length > 0 else b""
        consumer = getattr(self.server, "notify_consumer", None)
        if consumer is not None:
            try:
                consumer._dispatch_envelope(body)  # pylint: disable=protected-access
            except Exception:  # pylint: disable=broad-exception-caught
                pass
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        self.send_response(405)
        self.send_header("Content-Length", "0")
        self.end_headers()


class NotificationConsumerServer:
    """Embedded HTTP endpoint receiving ONVIF ``Notify`` SOAP POSTs.

    Implements the client side of the §7.7.4 *Notify* operation: the
    device pushes ``Notify`` SOAP envelopes to the URL returned by
    :attr:`consumer_reference`; every embedded
    ``wsnt:NotificationMessage`` is decoded into an :class:`OnvifEvent`
    and forwarded to *callback*.

    The server runs in its own background thread (``ThreadingHTTPServer``)
    so the main loop is never blocked. Both :meth:`start` and
    :meth:`stop` are idempotent.
    """

    def __init__(
        self,
        callback: Callable[[OnvifEvent], None],
        *,
        host: str = "",
        port: int = 0,
        path: str = "/onvif/notify",
        advertised_host: Optional[str] = None,
    ) -> None:
        self._callback = callback
        self._bind_host = host
        self._bind_port = port
        self._path = path if path.startswith("/") else f"/{path}"
        self._advertised_host = advertised_host
        self._server: Optional[ThreadingHTTPServer] = None
        self._thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()

    def start(self) -> "NotificationConsumerServer":
        """Bind the socket and start serving in a daemon thread."""
        with self._lock:
            if self._server is not None:
                return self
            srv = ThreadingHTTPServer(
                (self._bind_host, self._bind_port), _NotifyHandler,
            )
            srv.notify_consumer = self  # type: ignore[attr-defined]
            self._server = srv
            self._thread = threading.Thread(
                target=srv.serve_forever,
                name="onvif-notify-consumer",
                daemon=True,
            )
            self._thread.start()
        return self

    def stop(self, *, timeout: float = 4.0) -> None:
        """Shut down the HTTP server and join its thread. Idempotent."""
        with self._lock:
            srv = self._server
            thread = self._thread
            self._server = None
            self._thread = None
        if srv is not None:
            try:
                srv.shutdown()
            except Exception:  # pylint: disable=broad-exception-caught
                pass
            try:
                srv.server_close()
            except Exception:  # pylint: disable=broad-exception-caught
                pass
        if thread is not None:
            thread.join(timeout=timeout)

    @property
    def port(self) -> int:
        """Bound TCP port (0 until :meth:`start` runs)."""
        return self._server.server_port if self._server is not None else 0

    @property
    def is_running(self) -> bool:
        return self._thread is not None and self._thread.is_alive()

    @property
    def consumer_reference(self) -> str:
        """URL the camera should POST ``Notify`` envelopes to.

        Built from the bound port plus either *advertised_host*, the
        bind host (when explicit), or the best-effort local LAN IP.
        Empty string until :meth:`start` runs.
        """
        if self._server is None:
            return ""
        host = self._advertised_host or self._bind_host or _autodetect_local_ip()
        return f"http://{host}:{self.port}{self._path}"

    def _dispatch_envelope(self, body: bytes) -> None:
        """Parse a SOAP ``Notify`` body and dispatch every message."""
        if not body or not _HAS_LXML:
            return
        try:
            root = _etree.fromstring(body)  # noqa: S320  (no DTD, no external entity loading)
        except Exception:  # pylint: disable=broad-exception-caught
            return
        for notif in root.iter(f"{{{_NS_WSNT}}}NotificationMessage"):
            event = _parse_notify_xml_message(notif)
            try:
                self._callback(event)
            except Exception:  # pylint: disable=broad-exception-caught
                pass


def _autodetect_local_ip() -> str:
    """Best-effort LAN IP for the SubscriptionReference URL."""
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sk:
            sk.connect(("8.8.8.8", 80))
            return sk.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def _parse_notify_xml_message(notif: Any) -> OnvifEvent:
    """Convert one ``wsnt:NotificationMessage`` lxml node into an OnvifEvent."""
    ev = OnvifEvent()
    topic_el = notif.find(f"{{{_NS_WSNT}}}Topic")
    if topic_el is not None and topic_el.text:
        ev.topic = topic_el.text.strip()
    producer_el = notif.find(f"{{{_NS_WSNT}}}ProducerReference")
    if producer_el is not None:
        addr = producer_el.find("{http://www.w3.org/2005/08/addressing}Address")
        if addr is not None and addr.text:
            ev.producer_reference = addr.text.strip()
    sub_el = notif.find(f"{{{_NS_WSNT}}}SubscriptionReference")
    if sub_el is not None:
        addr = sub_el.find("{http://www.w3.org/2005/08/addressing}Address")
        if addr is not None and addr.text:
            ev.subscription_reference = addr.text.strip()

    msg_wrapper = notif.find(f"{{{_NS_WSNT}}}Message")
    if msg_wrapper is None:
        return ev
    inner = msg_wrapper.find(f"{{{_NS_TT}}}Message")
    if inner is None:
        inner = msg_wrapper[0] if len(msg_wrapper) else None
    if inner is None or not hasattr(inner, "get"):
        return ev
    ev.utc_time = _parse_iso_utc(inner.get("UtcTime") or "")
    ev.property_operation = inner.get("PropertyOperation") or ""
    source_el = inner.find(f"{{{_NS_TT}}}Source")
    data_el = inner.find(f"{{{_NS_TT}}}Data")
    ev.source = _decode_simple_items(source_el)
    ev.data = _decode_simple_items(data_el)
    return ev


# ---------------------------------------------------------------------------
# NotifyListener — daemon-thread Base-Notification listener
# ---------------------------------------------------------------------------

class NotifyListener:
    """High-level Base-Notification push pump.

    Combines :class:`NotificationConsumerServer` with
    :meth:`OnvifEventClient.subscribe_base_notification` and a periodic
    ``Renew`` heart-beat. Use as a context manager or call
    :meth:`start` / :meth:`stop` explicitly.
    """

    def __init__(  # pylint: disable=too-many-arguments
        self,
        client: OnvifEventClient,
        callback: Callable[[OnvifEvent], None],
        *,
        topic_filter: Optional[str] = None,
        initial_termination_time: str = "PT2M",
        renew_every_s: float = 60.0,
        renew_for: str = "PT2M",
        consumer_host: str = "",
        consumer_port: int = 0,
        consumer_path: str = "/onvif/notify",
        advertised_host: Optional[str] = None,
    ) -> None:
        self._client = client
        self._callback = callback
        self._topic_filter = topic_filter
        self._initial_termination_time = initial_termination_time
        self._renew_every_s = max(1.0, float(renew_every_s))
        self._renew_for = renew_for
        self._server = NotificationConsumerServer(
            self._on_event,
            host=consumer_host,
            port=consumer_port,
            path=consumer_path,
            advertised_host=advertised_host,
        )
        self._subscription_reference: str = ""
        self._stop = threading.Event()
        self._renew_thread: Optional[threading.Thread] = None
        self._lock = threading.Lock()

    def start(self) -> "NotifyListener":
        """Start the consumer server, send ``Subscribe`` and spawn the
        renew thread. Raises if the device rejects the subscription."""
        with self._lock:
            if self._renew_thread is not None and self._renew_thread.is_alive():
                return self
            self._stop.clear()
            self._server.start()
            response = self._client.subscribe_base_notification(
                consumer_reference=self._server.consumer_reference,
                topic_filter=self._topic_filter,
                initial_termination_time=self._initial_termination_time,
            )
            self._subscription_reference = _str(
                getattr(getattr(response, "SubscriptionReference", None), "Address", "")
            )
            self._renew_thread = threading.Thread(
                target=self._renew_loop,
                name="onvif-notify-renew",
                daemon=True,
            )
            self._renew_thread.start()
        return self

    def stop(self, *, timeout: float = 4.0) -> None:
        """Unsubscribe, stop the renew thread and the consumer server."""
        self._stop.set()
        with self._lock:
            renew_thread = self._renew_thread
            self._renew_thread = None
            sub_ref = self._subscription_reference
            self._subscription_reference = ""
        if renew_thread is not None:
            renew_thread.join(timeout=timeout)
        if sub_ref:
            self._client.unsubscribe_base_subscription(sub_ref)
        self._server.stop(timeout=timeout)

    def __enter__(self) -> "NotifyListener":
        return self.start()

    def __exit__(self, *_exc: Any) -> None:
        self.stop()

    @property
    def consumer_reference(self) -> str:
        return self._server.consumer_reference

    @property
    def subscription_reference(self) -> str:
        return self._subscription_reference

    @property
    def is_running(self) -> bool:
        return self._renew_thread is not None and self._renew_thread.is_alive()

    def _on_event(self, event: OnvifEvent) -> None:
        try:
            self._callback(event)
        except Exception:  # pylint: disable=broad-exception-caught
            pass

    def _renew_loop(self) -> None:
        while not self._stop.wait(self._renew_every_s):
            sub_ref = self._subscription_reference
            if not sub_ref:
                return
            try:
                self._client.renew_base_subscription(sub_ref, self._renew_for)
            except Exception:  # pylint: disable=broad-exception-caught
                # Re-subscribe if the device dropped us.
                try:
                    response = self._client.subscribe_base_notification(
                        consumer_reference=self._server.consumer_reference,
                        topic_filter=self._topic_filter,
                        initial_termination_time=self._initial_termination_time,
                    )
                    self._subscription_reference = _str(
                        getattr(getattr(response, "SubscriptionReference", None),
                                "Address", "")
                    )
                except Exception:  # pylint: disable=broad-exception-caught
                    pass


__all__ = [
    "EVENT_TOPICS",
    "TOPIC_DIALECT_CONCRETE",
    "OnvifEvent",
    "OnvifEventClient",
    "OnvifEventListener",
    "OnvifPullPointSubscription",
    "NotificationConsumerServer",
    "NotifyListener",
]
