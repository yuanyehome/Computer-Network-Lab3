# Computer-Network-Lab3-WT

> Ye Yuan
>
> 1700012821



- How do you estimate the bandwidth and the delay of a path?
  - Bandwidth: I record each time this client receive a body(in `on_body` function) and use the interval of two `on_body` call to estimate bandwidth;
  - Delay: When initiate the context, I will send a PING frame to server, and each I receive a PING-ack I will calculate the RTT to estimate the delay; and then send a PING again after receiving a PING-ack;
- How do you assign jobs to the three paths?
  - When a client is going to complete, which means left time is less than a rtt, I will judge whether it need to help other client. I will check other two clients and find the longest left time. If it is longer than delta+rtt, then I will calculate how to split the `will_be_downloaded_size` of another client, and then send a new request;
- What features (pipelining, eliminating tail byes, etc.) do you implement? And how do you implement them? You are encouraged to write down other design aspects of your implementation.
  - I have implemented pipelining as described before. I use PING frame and on-body calling to estimate bandwidth and delay, then send request before the fast client ends.