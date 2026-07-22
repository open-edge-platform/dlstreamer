# ==============================================================================
# Copyright (C) 2026 Intel Corporation
#
# SPDX-License-Identifier: MIT
# ==============================================================================
"""
PullPoint-based ONVIF Events engine — one instance per camera.

Manages the full subscription lifecycle:

- :meth:`subscribe`        — ``CreatePullPointSubscription``
- :meth:`pull`             — ``PullMessages`` (long-polling)
- :meth:`renew`            — extend the ``TerminationTime``
- :meth:`unsubscribe`      — close the subscription
- :meth:`stream`           — async generator that keeps pulling
- :meth:`get_event_properties` / :meth:`get_service_capabilities` /
  :meth:`get_supported_event_topics` — read-only introspection helpers

Every blocking method has an ``*_async`` variant that offloads to a
worker thread. The engine is safe to use from a single asyncio event
loop; it is not thread-safe for parallel calls from multiple threads
against the same instance.
"""
from __future__ import annotations

import asyncio
import inspect
from typing import (
    Any,
    AsyncIterator,
    Awaitable,
    Callable,
    List,
    Optional,
    Union,
)

from onvif import ONVIFCamera  # pylint: disable=import-error
from onvif.client import ONVIFService  # pylint: disable=import-error

from .types import EventCapableCamera, EventFilter, EventNotification, SupportedEventTopic


# ---------------------------------------------------------------------------
# Public callback type
# ---------------------------------------------------------------------------

# A per-event callback. May be a plain function or an ``async def`` coroutine
# function; both are supported by :meth:`OnvifEventEngine.stream_to_callback`.
EventCallback = Callable[[EventNotification], Union[None, Awaitable[None]]]


# ---------------------------------------------------------------------------
# WSDL binding for SubscriptionManager operations (Renew / Unsubscribe).
# ---------------------------------------------------------------------------

_SUBSCRIPTION_MANAGER_BINDING = (
    "{http://www.onvif.org/ver10/events/wsdl}SubscriptionManagerBinding"
)

_PULLPOINT_BINDING = (
    "{http://www.onvif.org/ver10/events/wsdl}PullPointSubscriptionBinding"
)


# ---------------------------------------------------------------------------
# Zeep → dataclass parsing
# ---------------------------------------------------------------------------


def _text(value: Any) -> str:
    """Coerce a zeep scalar (which may be ``None``) to string."""
    if value is None:
        return ""
    return str(value)


def _extract_simple_items(container: Any) -> dict[str, str]:
    """Flatten a ``Message.Source`` / ``Message.Data`` container into a dict."""
    if container is None:
        return {}
    items = getattr(container, "SimpleItem", None) or []
    return {
        _text(getattr(item, "Name", "")): _text(getattr(item, "Value", ""))
        for item in items
    }


def _is_lxml_element(obj: Any) -> bool:
    """Return True when *obj* is an lxml ``Element``."""
    return hasattr(obj, "tag") and hasattr(obj, "xpath")


_TT_NS = {"tt": "http://www.onvif.org/ver10/schema"}


def _extract_simple_items_xml(element: Any) -> dict[str, str]:
    """Extract ``SimpleItem`` children from an lxml Element."""
    items = element.xpath("tt:SimpleItem", namespaces=_TT_NS)
    return {
        (item.get("Name") or ""): (item.get("Value") or "")
        for item in items
    }


def _parse_notification(msg: Any) -> EventNotification:
    """Convert a zeep ``NotificationMessage`` into :class:`EventNotification`."""
    # Topic can be nested under Topic._value_1 (mixed content)
    topic_node = getattr(msg, "Topic", None)
    topic = ""
    if topic_node is not None:
        topic = _text(getattr(topic_node, "_value_1", topic_node))

    inner = getattr(msg, "Message", None)
    # Some cameras wrap the payload in Message._value_1 (mixed content).
    # ``_value_*`` is zeep's convention for mixed-content payloads.
    if inner is not None and hasattr(inner, "_value_1"):
        inner = inner._value_1  # pylint: disable=protected-access

    if inner is None:
        return EventNotification(topic=topic, raw=msg)

    # --- lxml Element path (vendor cameras that return raw XML) ---
    if _is_lxml_element(inner):
        src_el = inner.xpath("tt:Source", namespaces=_TT_NS)
        dat_el = inner.xpath("tt:Data", namespaces=_TT_NS)
        return EventNotification(
            topic=topic,
            utc_time=inner.get("UtcTime") or "",
            property_operation=inner.get("PropertyOperation") or "",
            source=_extract_simple_items_xml(src_el[0]) if src_el else {},
            data=_extract_simple_items_xml(dat_el[0]) if dat_el else {},
            raw=msg,
        )

    # --- zeep object path (standard ONVIF cameras) ---
    return EventNotification(
        topic=topic,
        utc_time=_text(getattr(inner, "UtcTime", "")),
        property_operation=_text(getattr(inner, "PropertyOperation", "")),
        source=_extract_simple_items(getattr(inner, "Source", None)),
        data=_extract_simple_items(getattr(inner, "Data", None)),
        raw=msg,
    )


# ---------------------------------------------------------------------------
# GetEventProperties TopicSet parsing
# ---------------------------------------------------------------------------

# Marks a node in the WS-Topics tree as a concrete (subscribable) topic.
_WSTOP_TOPIC_ATTR = "{http://docs.oasis-open.org/wsn/t-1}topic"

# ``MessageDescription`` (and its Source/Data blocks) live in the ONVIF schema
# namespace; they describe the payload each topic emits.
_MESSAGE_DESCRIPTION = f"{{{_TT_NS['tt']}}}MessageDescription"


def _local_name(tag: Any) -> str:
    """Return the local part of an lxml tag, or ``""`` for comments/PIs."""
    return tag.split("}")[-1] if isinstance(tag, str) else ""


def _parse_item_descriptions(container: Any) -> dict[str, str]:
    """Map ``{name: type}`` for a message block's item descriptions.

    Covers both ``SimpleItemDescription`` (scalar fields, e.g.
    ``IsMotion: xsd:boolean``) and ``ElementItemDescription`` (complex-typed
    fields, e.g. ``ProfileToken: tt:ProfileReference``); both carry ``Name``
    and ``Type`` attributes.
    """
    out: dict[str, str] = {}
    for item in container:
        if _local_name(getattr(item, "tag", "")) in (
            "SimpleItemDescription",
            "ElementItemDescription",
        ):
            name = item.get("Name") or ""
            if name:
                out[name] = item.get("Type") or ""
    return out


def _parse_message_description(topic_el: Any) -> tuple[bool, dict[str, str], dict[str, str]]:
    """Extract ``(is_property, source, data)`` from a topic's MessageDescription."""
    for child in topic_el:
        if getattr(child, "tag", "") != _MESSAGE_DESCRIPTION:
            continue
        source: dict[str, str] = {}
        data: dict[str, str] = {}
        for block in child:
            local = _local_name(getattr(block, "tag", ""))
            if local == "Source":
                source = _parse_item_descriptions(block)
            elif local == "Data":
                data = _parse_item_descriptions(block)
        is_property = str(child.get("IsProperty", "")).lower() == "true"
        return is_property, source, data
    return False, {}, {}


def _collect_topics(elements: Any, path: str, out: List[SupportedEventTopic]) -> None:
    """Recursively collect described topics from an iterable of elements.

    ``elements`` is any iterable of lxml topic nodes (a list of roots or an
    lxml Element, whose iteration yields its children). ``MessageDescription``
    branches and comments/PIs are skipped as topic nodes.
    """
    for el in elements:
        tag = getattr(el, "tag", "")
        if not isinstance(tag, str) or tag == _MESSAGE_DESCRIPTION:
            continue
        local = tag.split("}")[-1]
        prefix = getattr(el, "prefix", None)
        name = f"{prefix}:{local}" if prefix else local
        el_path = f"{path}/{name}" if path else name
        if str(el.get(_WSTOP_TOPIC_ATTR, "")).lower() == "true":
            is_property, source, data = _parse_message_description(el)
            out.append(
                SupportedEventTopic(
                    topic=el_path,
                    is_property=is_property,
                    source=source,
                    data=data,
                )
            )
        _collect_topics(el, el_path, out)


def _parse_topic_set(topic_set: Any) -> List[SupportedEventTopic]:
    """Flatten a ``GetEventProperties`` TopicSet into described topics.

    Returns a list of :class:`SupportedEventTopic`, sorted by topic path and
    de-duplicated, each carrying the topic's ``MessageDescription`` schema
    (Source/Data item names and types). Handles the zeep ``xsd:any`` container
    (children in ``_value_1``) as well as a bare lxml Element.
    """
    if topic_set is None:
        return []

    roots: List[Any] = []
    for attr in ("_value_1", "_value_2"):
        value = getattr(topic_set, attr, None)
        if value is None:
            continue
        roots.extend(value if isinstance(value, list) else [value])
    if not roots and _is_lxml_element(topic_set):
        roots.append(topic_set)

    collected: List[SupportedEventTopic] = []
    _collect_topics(roots, "", collected)

    by_topic: dict[str, SupportedEventTopic] = {}
    for topic in collected:
        by_topic.setdefault(topic.topic, topic)
    return [by_topic[key] for key in sorted(by_topic)]


# ---------------------------------------------------------------------------
# Engine
# ---------------------------------------------------------------------------


class OnvifEventEngine:
    """PullPoint subscription manager for a single ONVIF camera.

    Typical usage::

        with OnvifEventEngine("192.168.1.10", 80, "admin", "…") as eng:
            eng.subscribe(termination_time="PT1H")
            while True:
                for note in eng.pull(timeout="PT30S", limit=100):
                    print(note.short())

    For long-running pulls, call :meth:`renew` before the initial
    termination time expires (typically every ~half of ``PT1H``). The
    :meth:`stream` async generator handles auto-renewal for you.
    """

    def __init__(
        self,
        hostname: str,
        port: int,
        username: str = "",
        password: str = "",
        verbose: bool = False,
    ) -> None:
        self.hostname = hostname
        self.port = int(port)
        self.username = username
        self.password = password
        self.verbose = verbose

        self._client: Optional[ONVIFCamera] = None
        self._events = None
        self._pullpoint = None
        self._subscription_manager: Optional[ONVIFService] = None
        self._subscribed: bool = False
        self._subscription_reference: str = ""
        self._termination_time: str = ""

    # ---- construction ----

    @classmethod
    def from_capable_camera(
        cls,
        camera: EventCapableCamera,
        username: str = "",
        password: str = "",
        verbose: bool = False,
    ) -> "OnvifEventEngine":
        """Build an engine directly from an :class:`EventCapableCamera`."""
        return cls(
            hostname=camera.hostname,
            port=camera.port,
            username=username,
            password=password,
            verbose=verbose,
        )

    # ---- session lifecycle ----

    def _get_client(self) -> ONVIFCamera:
        if self._client is None:
            self._client = ONVIFCamera(
                self.hostname, self.port, self.username, self.password
            )
        return self._client

    def _events_service(self):
        if self._events is None:
            self._events = self._get_client().create_events_service()
        return self._events

    def _pullpoint_service(self):
        if self._pullpoint is None:
            if not self._subscribed:
                raise RuntimeError(
                    "PullPoint subscription is not active; call subscribe() first."
                )
            if not self._subscription_reference:
                # Fallback for cameras that don't return a reference URL.
                self._pullpoint = self._get_client().create_pullpoint_service()
            else:
                # Build the service manually, pointing at the subscription
                # reference URL returned by CreatePullPointSubscription.
                cam = self._get_client()
                _, wsdl_file, _ = cam.get_definition("events")
                self._pullpoint = ONVIFService(
                    xaddr=self._subscription_reference,
                    user=self.username,
                    passwd=self.password,
                    url=wsdl_file,
                    encrypt=cam.encrypt,
                    daemon=cam.daemon,
                    no_cache=cam.no_cache,
                    portType=None,
                    dt_diff=cam.dt_diff,
                    binding_name=_PULLPOINT_BINDING,
                    transport=cam.transport,
                )
        return self._pullpoint

    def _subscription_manager_service(self) -> ONVIFService:
        """Bind a SubscriptionManager to the current subscription reference URL.

        The ONVIF PullPoint binding only exposes ``PullMessages``/``Seek``/
        ``SetSynchronizationPoint``. ``Renew`` and ``Unsubscribe`` live on
        the ``SubscriptionManagerBinding`` in the same events WSDL but at
        the per-subscription URL returned by
        ``CreatePullPointSubscription``. onvif-zeep does not expose a
        factory for it, so we build one manually.
        """
        if not self._subscribed or not self._subscription_reference:
            raise RuntimeError(
                "PullPoint subscription is not active; call subscribe() first."
            )
        if self._subscription_manager is not None:
            return self._subscription_manager

        cam = self._get_client()
        # Reuse the events.wsdl file shipped with onvif-zeep.
        _, wsdl_file, _ = cam.get_definition("events")
        self._subscription_manager = ONVIFService(
            xaddr=self._subscription_reference,
            user=self.username,
            passwd=self.password,
            url=wsdl_file,
            encrypt=cam.encrypt,
            daemon=cam.daemon,
            no_cache=cam.no_cache,
            portType=None,
            dt_diff=cam.dt_diff,
            binding_name=_SUBSCRIPTION_MANAGER_BINDING,
            transport=cam.transport,
        )
        return self._subscription_manager

    @property
    def subscribed(self) -> bool:
        """True while a PullPoint subscription is active."""
        return self._subscribed

    @property
    def subscription_reference(self) -> str:
        """Subscription reference URL returned by CreatePullPointSubscription."""
        return self._subscription_reference

    @property
    def termination_time(self) -> str:
        """Latest ``TerminationTime`` reported by the camera (or empty)."""
        return self._termination_time

    def close(self) -> None:
        """Unsubscribe (best-effort) and drop cached zeep clients."""
        if self._subscribed:
            try:
                self.unsubscribe()
            except Exception:  # pylint: disable=broad-exception-caught
                pass
        self._events = None
        self._pullpoint = None
        self._subscription_manager = None
        self._client = None

    def __enter__(self) -> "OnvifEventEngine":
        # Warm up the ONVIF client so any connectivity error surfaces here,
        # not on the first API call.
        self._get_client()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    # ---- introspection ----

    def get_service_capabilities(self) -> dict:
        """Return the ``EventServiceCapabilities`` flattened to a dict."""
        raw = self._events_service().GetServiceCapabilities()
        return {
            "ws_subscription_policy_support": bool(
                getattr(raw, "WSSubscriptionPolicySupport", False)
            ),
            "ws_pull_point_support": bool(
                getattr(raw, "WSPullPointSupport", False)
            ),
            "ws_pausable_subscription_manager_interface_support": bool(
                getattr(
                    raw, "WSPausableSubscriptionManagerInterfaceSupport", False
                )
            ),
            "max_notification_producers": int(
                getattr(raw, "MaxNotificationProducers", 0) or 0
            ),
            "max_pull_points": int(getattr(raw, "MaxPullPoints", 0) or 0),
            "persistent_notification_storage": bool(
                getattr(raw, "PersistentNotificationStorage", False)
            ),
        }

    def get_event_properties(self) -> Any:
        """Return the raw ``GetEventProperties`` response (topic set + dialects)."""
        return self._events_service().GetEventProperties()

    def get_supported_event_topics(self) -> List[SupportedEventTopic]:
        """Return the event types the camera advertises, with their schema.

        Calls ``GetEventProperties`` and flattens the returned ``TopicSet``
        into a sorted list of :class:`SupportedEventTopic`. Each entry carries
        the topic path (e.g. ``"tns1:RuleEngine/CellMotionDetector/Motion"``)
        together with its ``MessageDescription`` — the Source/Data field names
        and types the notification will contain (e.g.
        ``data={"IsMotion": "xsd:boolean"}``) and the ``IsProperty`` flag. Any
        topic may be used as an
        :class:`~dlstreamer.onvif.event_manager.EventFilter` expression.
        Returns an empty list when the camera exposes no topics.
        """
        props = self._events_service().GetEventProperties()
        return _parse_topic_set(getattr(props, "TopicSet", None))

    # ---- subscription lifecycle ----

    def subscribe(
        self,
        termination_time: str = "PT1H",
        topic_filter: Optional[EventFilter] = None,
    ) -> None:
        """Create a PullPoint subscription.

        Args:
            termination_time: Initial ``TerminationTime`` as an ISO 8601
                duration (e.g. ``"PT1H"``). Renew before it expires.
            topic_filter: Optional single :class:`EventFilter`. ONVIF
                subscriptions support at most one topic filter; omit for
                the default "all topics".

        Raises:
            RuntimeError: if a subscription is already active on this
                engine (call :meth:`unsubscribe` first).
        """
        if self._subscribed:
            raise RuntimeError(
                "Already subscribed; call unsubscribe() or close() first."
            )

        request: dict = {"InitialTerminationTime": termination_time}
        if topic_filter and topic_filter.expression:
            request["Filter"] = {
                "TopicExpression": {
                    "_value_1": topic_filter.expression,
                    "Dialect": topic_filter.dialect,
                }
            }

        response = self._events_service().CreatePullPointSubscription(request)

        # Extract subscription reference URL if present (informational).
        ref_addr = ""
        try:
            addr = response.SubscriptionReference.Address
            ref_addr = _text(getattr(addr, "_value_1", addr))
        except AttributeError:
            pass
        self._subscription_reference = ref_addr
        self._termination_time = _text(getattr(response, "TerminationTime", ""))
        self._pullpoint = None  # will be created on first use
        self._subscription_manager = None  # will be built on first renew/unsub
        self._subscribed = True

        if self.verbose:
            print(
                f"[event] subscribed: ref={ref_addr or '-'} "
                f"term={self._termination_time or '-'}"
            )

    def pull(
        self,
        timeout: str = "PT30S",
        limit: int = 100,
    ) -> List[EventNotification]:
        """Fetch a batch of notifications (long-polling).

        Blocks up to ``timeout`` seconds waiting for events. Returns an
        empty list if nothing arrived within the window.
        """
        response = self._pullpoint_service().PullMessages(
            {"Timeout": timeout, "MessageLimit": int(limit)}
        )
        messages = getattr(response, "NotificationMessage", None) or []
        current_term = getattr(response, "TerminationTime", None)
        if current_term is not None:
            self._termination_time = _text(current_term)
        return [_parse_notification(m) for m in messages]

    def renew(self, termination_time: str = "PT1H") -> None:
        """Extend the subscription with a new ``TerminationTime``."""
        response = self._subscription_manager_service().Renew(
            {"TerminationTime": termination_time}
        )
        current_term = getattr(response, "TerminationTime", None)
        if current_term is not None:
            self._termination_time = _text(current_term)
        if self.verbose:
            print(f"[event] renewed: term={self._termination_time or '-'}")

    def unsubscribe(self) -> None:
        """Cancel the subscription and clear PullPoint state."""
        if not self._subscribed:
            return
        try:
            self._subscription_manager_service().Unsubscribe({})
        except Exception as exc:  # pylint: disable=broad-exception-caught
            if self.verbose:
                print(
                    f"[event] unsubscribe error (ignored): "
                    f"{type(exc).__name__}: {exc}"
                )
        finally:
            self._pullpoint = None
            self._subscription_manager = None
            self._subscribed = False
            self._subscription_reference = ""
            self._termination_time = ""

    # ---- streaming ----

    async def stream(
        self,
        timeout: str = "PT30S",
        limit: int = 100,
        renew_every: Optional[float] = 300.0,
    ) -> AsyncIterator[EventNotification]:
        """Yield notifications continuously.

        Repeatedly calls :meth:`pull`; if ``renew_every`` is set (default
        5 minutes), also refreshes the subscription in the background so
        it does not expire between pulls.

        Requires an active subscription (:meth:`subscribe`). The stream
        stops when the caller breaks out of the ``async for`` loop or an
        underlying SOAP call raises.
        """
        if not self._subscribed:
            raise RuntimeError("Not subscribed; call subscribe() first.")

        renew_task: Optional[asyncio.Task] = None
        stop = asyncio.Event()

        async def _renewer() -> None:
            assert renew_every is not None
            try:
                while not stop.is_set():
                    try:
                        await asyncio.wait_for(stop.wait(), timeout=renew_every)
                        return  # stopped
                    except asyncio.TimeoutError:
                        try:
                            await asyncio.to_thread(self.renew, "PT1H")
                        except Exception as exc:  # pylint: disable=broad-exception-caught
                            if self.verbose:
                                print(
                                    f"[event] renew failed: "
                                    f"{type(exc).__name__}: {exc}"
                                )
            except asyncio.CancelledError:
                pass

        if renew_every is not None and renew_every > 0:
            renew_task = asyncio.create_task(_renewer())

        try:
            while True:
                batch = await asyncio.to_thread(self.pull, timeout, limit)
                for note in batch:
                    yield note
        finally:
            stop.set()
            if renew_task is not None:
                renew_task.cancel()
                try:
                    await renew_task
                except asyncio.CancelledError:
                    pass
                except Exception:  # pylint: disable=broad-exception-caught
                    pass

    async def stream_to_callback(
        self,
        callback: EventCallback,
        timeout: str = "PT30S",
        limit: int = 100,
        renew_every: Optional[float] = 300.0,
    ) -> None:
        """Invoke ``callback`` for every notification as it arrives.

        Thin wrapper around :meth:`stream`: keeps pulling notifications
        (auto-renewing the subscription per ``renew_every``) and calls
        ``callback`` once per :class:`EventNotification`.

        The callback may be a plain function or an ``async def`` coroutine
        function; coroutine results are awaited. Runs until the caller
        cancels the surrounding task or an underlying SOAP call raises.

        Requires an active subscription (:meth:`subscribe`).
        """
        async for note in self.stream(
            timeout=timeout,
            limit=limit,
            renew_every=renew_every,
        ):
            result = callback(note)
            if inspect.isawaitable(result):
                await result

    # ---- async wrappers ----

    async def subscribe_async(
        self,
        termination_time: str = "PT1H",
        topic_filter: Optional[EventFilter] = None,
    ) -> None:
        await asyncio.to_thread(self.subscribe, termination_time, topic_filter)

    async def pull_async(
        self,
        timeout: str = "PT30S",
        limit: int = 100,
    ) -> List[EventNotification]:
        return await asyncio.to_thread(self.pull, timeout, limit)

    async def renew_async(self, termination_time: str = "PT1H") -> None:
        await asyncio.to_thread(self.renew, termination_time)

    async def unsubscribe_async(self) -> None:
        await asyncio.to_thread(self.unsubscribe)

    async def get_service_capabilities_async(self) -> dict:
        return await asyncio.to_thread(self.get_service_capabilities)

    async def get_event_properties_async(self) -> Any:
        return await asyncio.to_thread(self.get_event_properties)

    async def get_supported_event_topics_async(self) -> List[SupportedEventTopic]:
        return await asyncio.to_thread(self.get_supported_event_topics)
